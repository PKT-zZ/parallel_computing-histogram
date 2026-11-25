// ============================================================================
// Tệp: src/histogram_local.cpp
// Tác giả: Thành viên 2 - Histogram cục bộ và phân tích hiệu năng
// Mô tả: Cài đặt thuật toán tính Histogram song song bằng OpenMP theo phương pháp
//        mảng đếm cục bộ cho từng thread (Local Histogram) rồi gộp (Reduction).
//        Kiểm soát bộ nhớ O(P * B) và đảm bảo đồng bộ trước bước gộp.
// ============================================================================

// Nạp tệp header khai báo cấu trúc HistogramResult và chữ ký các hàm
#include "histogram.hpp"

// Nạp thư viện OpenMP để sử dụng các hàm omp_get_thread_num, omp_get_num_threads, omp_get_wtime
#include <omp.h>
// Nạp thư viện stdexcept để ném ngoại lệ std::invalid_argument khi đầu vào sai
#include <stdexcept>
// Nạp thư viện algorithm (dự phòng cho các thao tác chuẩn)
#include <algorithm>

// Mở không gian tên hist tương ứng với tệp header
namespace hist {

// Hằng số kích thước một dòng cache (Cache Line) phổ biến trên CPU x86-64 là 64 byte
constexpr size_t CACHE_LINE_BYTES = 64;
// Số phần tử uint64_t (mỗi phần tử 8 byte) vừa khít trong một dòng cache (64 / 8 = 8)
constexpr size_t U64_PER_CACHE_LINE = CACHE_LINE_BYTES / sizeof(uint64_t);

// ----------------------------------------------------------------------------
// Hàm nội bộ: Tính bước nhảy (stride) theo số phần tử uint64_t giữa hai thread
// Nếu không dùng padding: stride = num_bins (tốn đúng P * B phần tử)
// Nếu dùng padding: làm tròn lên bội số của U64_PER_CACHE_LINE (8) để tách cache line
// ----------------------------------------------------------------------------
static size_t compute_stride(size_t num_bins, bool use_padding) noexcept {
    // Nếu không bật chế độ padding thì trả về chính xác số bin B
    if (!use_padding) {
        return num_bins;
    }
    // Tính số cache line cần thiết tối thiểu để chứa num_bins phần tử uint64_t
    size_t lines_needed = (num_bins + U64_PER_CACHE_LINE - 1) / U64_PER_CACHE_LINE;
    // Nhân ngược lại với số phần tử trên mỗi cache line để được bội số của 8
    return lines_needed * U64_PER_CACHE_LINE;
}

// ----------------------------------------------------------------------------
// Hàm ước lượng bộ nhớ phụ trợ tiêu tốn (tính bằng byte)
// Giúp lập bảng phân tích chi phí bộ nhớ O(P * B) trong báo cáo
// ----------------------------------------------------------------------------
size_t estimate_local_memory_bytes(int num_threads, size_t num_bins, bool use_padding) noexcept {
    // Nếu số thread không dương hoặc số bin bằng 0 thì không tốn bộ nhớ phụ trợ
    if (num_threads <= 0 || num_bins == 0) {
        return 0;
    }
    // Tính khoảng cách giữa hai mảng cục bộ liền kề (tính theo số phần tử uint64_t)
    size_t stride = compute_stride(num_bins, use_padding);
    // Tổng bộ nhớ = số thread * bước nhảy * kích thước 1 phần tử uint64_t (8 byte)
    return static_cast<size_t>(num_threads) * stride * sizeof(uint64_t);
}

// ----------------------------------------------------------------------------
// Hàm lõi dùng chung cho cả phiên bản tiêu chuẩn và phiên bản có padding
// Tham số use_padding quyết định có căn chỉnh chống false sharing hay không
// ----------------------------------------------------------------------------
static HistogramResult compute_histogram_local_impl(
    const std::vector<uint8_t>& data,
    size_t num_bins,
    int requested_threads,
    bool use_padding
) {
    // Kiểm tra hợp lệ: số bin B phải >= 1, nếu bằng 0 thì báo lỗi rõ ràng
    if (num_bins == 0) {
        throw std::invalid_argument("Loi: So bin (num_bins) phai lon hon hoac bang 1.");
    }
    // Kiểm tra hợp lệ: số thread yêu cầu phải >= 1
    if (requested_threads <= 0) {
        throw std::invalid_argument("Loi: So thread yeu cau (requested_threads) phai lon hon hoac bang 1.");
    }

    // Khởi tạo đối tượng lưu kết quả trả về
    HistogramResult result;

    // Bắt đầu bấm giờ T_total bằng đồng hồ thời gian thực độ phân giải cao của OpenMP
    // Theo mục 3.2: T_total bắt đầu NGAY TRƯỚC khi khởi tạo bộ đếm và cấp phát mảng cục bộ
    double t_start = omp_get_wtime();

    // Cấp phát và khởi tạo mảng histogram kết quả cuối cùng gồm num_bins phần tử bằng 0
    result.bins.assign(num_bins, 0ULL);

    // Lấy tổng số phần tử N của mảng dữ liệu đầu vào
    const size_t n = data.size();

    // Xử lý trường hợp biên N == 0 (mảng rỗng)
    if (n == 0) {
        // Vẫn mở nhanh một vùng song song với num_threads để ghi nhận số thread thực tế
        // mà OpenMP cấp phát cho cấu hình này, đảm bảo nhất quán khi kiểm thử
        int actual_p = 1;
        #pragma omp parallel num_threads(requested_threads)
        {
            // Chỉ một thread duy nhất (single) đọc tổng số thread thực tế để tránh ghi đè
            #pragma omp single
            {
                actual_p = omp_get_num_threads();
            }
        }
        // Lưu số thread thực tế vào kết quả
        result.actual_threads = actual_p;
        // Dừng đồng hồ và tính thời gian thực thi (giây)
        result.elapsed_seconds = omp_get_wtime() - t_start;
        // Trả về mảng bins toàn số 0 (tổng bin = 0 == N)
        return result;
    }

    // Tính bước nhảy stride giữa các mảng cục bộ của từng thread
    const size_t stride = compute_stride(num_bins, use_padding);

    // Con trỏ hằng trỏ thẳng tới vùng nhớ liên tiếp của mảng đầu vào để tối ưu truy cập
    const uint8_t* raw_data = data.data();

    // Khai báo mảng phẳng 1 chiều chứa toàn bộ histogram cục bộ của các thread
    // Kích thước thực tế sẽ được cấp phát đúng bằng (actual_threads * stride)
    // nhằm kiểm soát chặt bộ nhớ ở mức O(P * B) thay vì dùng vector 2 chiều rời rạc
    std::vector<uint64_t> local_hists;

    // Biến lưu số thread thực tế mà hệ thống OpenMP cấp phát
    int actual_threads = 1;

    // Mở vùng tính toán song song với đúng số thread mong muốn (requested_threads)
    // Lưu ý: dùng mệnh đề num_threads(...) thay vì thay đổi trạng thái toàn cục
    #pragma omp parallel num_threads(requested_threads)
    {
        // Lấy mã định danh duy nhất của thread hiện tại (từ 0 đến actual_threads - 1)
        const int tid = omp_get_thread_num();

        // Chỉ một thread duy nhất thực hiện việc ghi nhận actual_threads và cấp phát local_hists
        // Mệnh đề single có hàng rào đồng bộ ngầm (implicit barrier) ở cuối khối,
        // đảm bảo mảng local_hists đã được cấp phát xong hoàn toàn trước khi các thread khác ghi vào
        #pragma omp single
        {
            // Đọc số thread thực tế được OpenMP cấp trong vùng parallel này
            actual_threads = omp_get_num_threads();
            // Cấp phát một khối nhớ liên tiếp kích thước O(P * B) và khởi tạo toàn bộ bằng 0
            local_hists.assign(static_cast<size_t>(actual_threads) * stride, 0ULL);
        }
        // <--- Implicit barrier tại đây: mọi thread chắc chắn thấy local_hists đã sẵn sàng!

        // Tính con trỏ trỏ tới vùng mảng histogram cục bộ RIÊNG BIỆT của thread hiện tại
        // Thread tid chỉ đọc và ghi trong đoạn [tid * stride, tid * stride + num_bins - 1]
        // Vì các đoạn này hoàn toàn rời nhau, KHÔNG BAO GIỜ xảy ra data race giữa các thread
        uint64_t* my_hist = local_hists.data() + static_cast<size_t>(tid) * stride;

        // Chia vòng lặp N phần tử thành các khối liên tiếp đều nhau (schedule(static))
        // Do chi phí xử lý mỗi phần tử là như nhau, static scheduling giảm thiểu chi phí điều phối
        // và tối ưu tính cục bộ cache (spatial locality) khi đọc raw_data tuần tự
        #pragma omp for schedule(static)
        for (size_t i = 0; i < n; ++i) {
            // Ánh xạ giá trị uint8_t sang chỉ số bin: floor(x * B / 256)
            const size_t bin_idx = map_value_to_bin(raw_data[i], num_bins);
            // Tăng bộ đếm cục bộ của riêng thread này (không cần atomic, không tranh chấp)
            my_hist[bin_idx] += 1ULL;
        }
        // <--- Implicit barrier tại cuối #pragma omp for và cuối #pragma omp parallel:
        //      ĐẢM BẢO 100% mọi thread đã hoàn tất việc đếm phần dữ liệu của mình
        //      trước khi bước sang giai đoạn gộp bin phía dưới!
    }

    // ------------------------------------------------------------------------
    // Giai đoạn gộp (Reduction) tuần tự theo từng bin
    // Chi phí: O(P * B) phép cộng (với P <= 32 và B <= 256 thì chỉ tối đa vài nghìn phép cộng,
    // cực nhỏ so với N hàng triệu phần tử, đúng theo thiết kế mục 2.2)
    // ------------------------------------------------------------------------
    // Lấy con trỏ tới mảng kết quả cuối cùng để tăng tốc truy cập trong vòng lặp
    uint64_t* final_bins = result.bins.data();

    // Duyệt qua từng thread t từ 0 đến actual_threads - 1
    // (Đặt vòng lặp thread bên ngoài và bin bên trong giúp truy cập tuần tự liên tiếp
    //  vào mảng local_hists theo từng dòng cache, tối ưu cache hit hơn so với duyệt ngược lại)
    for (int t = 0; t < actual_threads; ++t) {
        // Trỏ tới mảng cục bộ của thread t
        const uint64_t* thread_hist = local_hists.data() + static_cast<size_t>(t) * stride;
        // Cộng dồn từng bin của thread t vào mảng kết quả cuối cùng
        for (size_t b = 0; b < num_bins; ++b) {
            final_bins[b] += thread_hist[b];
        }
    }

    // Dừng đồng hồ ngay sau khi mảng kết quả cuối cùng đã hoàn tất
    double t_end = omp_get_wtime();

    // Lưu số thread thực tế đã sử dụng vào cấu trúc kết quả
    result.actual_threads = actual_threads;
    // Lưu tổng thời gian thực thi T_total (đơn vị: giây)
    result.elapsed_seconds = t_end - t_start;

    // Trả về kết quả đầy đủ cho hàm gọi
    return result;
}

// ----------------------------------------------------------------------------
// Hàm công khai 1: Phiên bản Histogram Cục bộ tiêu chuẩn (bắt buộc của Thành viên 2)
// ----------------------------------------------------------------------------
HistogramResult compute_histogram_local(
    const std::vector<uint8_t>& data,
    size_t num_bins,
    int requested_threads
) {
    // Gọi hàm lõi với use_padding = false (bộ nhớ chặt đúng P * B * 8 byte)
    return compute_histogram_local_impl(data, num_bins, requested_threads, false);
}

// ----------------------------------------------------------------------------
// Hàm công khai 2: Phiên bản Histogram Cục bộ có Padding (phần mở rộng mục 7)
// ----------------------------------------------------------------------------
HistogramResult compute_histogram_local_padded(
    const std::vector<uint8_t>& data,
    size_t num_bins,
    int requested_threads
) {
    // Gọi hàm lõi với use_padding = true (căn chỉnh mỗi hàng theo cache line 64 byte)
    return compute_histogram_local_impl(data, num_bins, requested_threads, true);
}

} // namespace hist
