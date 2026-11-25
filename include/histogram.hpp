// ============================================================================
// Tệp: include/histogram.hpp
// Tác giả: Thành viên 2 (Giao diện dùng chung cho toàn nhóm tích hợp)
// Mô tả: Khai báo cấu trúc kết quả, hàm ánh xạ giá trị sang bin và giao diện
//        các phiên bản tính Histogram (Tuần tự, Atomic, Cục bộ & Gộp).
// ============================================================================

// Tránh việc tệp header bị include nhiều lần trong cùng một đơn vị biên dịch
#pragma once

// Cung cấp các kiểu số nguyên có kích thước cố định (uint8_t, uint64_t)
#include <cstdint>
// Cung cấp kiểu size_t để biểu diễn kích thước mảng và chỉ số không âm
#include <cstddef>
// Cung cấp cấu trúc mảng động std::vector để lưu trữ dữ liệu đầu vào và kết quả bin
#include <vector>

// Định nghĩa không gian tên chung của đồ án để tránh xung đột tên hàm/biến
namespace hist {

// ----------------------------------------------------------------------------
// Cấu trúc lưu trữ kết quả thực thi của các thuật toán Histogram song song
// Mục đích: Trả về đồng thời mảng đếm bin, số thread thực tế OpenMP đã cấp
//           và thời gian thực thi chính xác theo phạm vi đo quy định.
// ----------------------------------------------------------------------------
struct HistogramResult {
    // Mảng đếm kết quả có kích thước B; bins[b] là số phần tử rơi vào bin b
    std::vector<uint64_t> bins;
    // Số lượng thread thực tế được OpenMP cấp phát bên trong vùng song song
    // (Có thể khác số thread yêu cầu do giới hạn phần cứng hoặc biến môi trường)
    int actual_threads = 1;
    // Tổng thời gian thực thi (đơn vị: giây), tính từ trước khi khởi tạo bộ đếm
    // cho tới khi hoàn tất mảng kết quả cuối cùng (bao gồm cấp phát & gộp bin)
    double elapsed_seconds = 0.0;
};

// ----------------------------------------------------------------------------
// Hàm nội tuyến (inline) tính chỉ số bin tương ứng cho một giá trị đầu vào x
// Công thức theo đặc tả mục 2.1: bin = floor(x * B / 256)
// Tham số:
//   - x        : Giá trị phần tử kiểu uint8_t nằm trong đoạn [0, 255]
//   - num_bins : Tổng số bin B (thường dùng 16, 64 hoặc 256)
// Giá trị trả về: Chỉ số bin hợp lệ nằm trong đoạn [0, num_bins - 1]
// ----------------------------------------------------------------------------
inline size_t map_value_to_bin(uint8_t x, size_t num_bins) noexcept {
    // Ép kiểu x sang size_t trước khi nhân với num_bins để tránh tràn số 8-bit,
    // sau đó chia nguyên cho 256 (tương đương phép dịch phải >> 8 và lấy floor)
    return (static_cast<size_t>(x) * num_bins) >> 8;
}

// ----------------------------------------------------------------------------
// [Thành viên 2] Phiên bản Histogram Cục bộ và Gộp (Local Histogram + Reduction)
// Cơ chế: Mỗi thread đếm vào mảng cục bộ riêng O(B) nằm trong vùng nhớ O(P*B),
//         đảm bảo không có xung đột ghi (data race). Sau khi mọi thread đếm xong,
//         tiến hành gộp tuần tự từng bin về mảng kết quả chung.
// Tham số:
//   - data              : Mảng dữ liệu đầu vào gồm N phần tử uint8_t (0..255)
//   - num_bins          : Số lượng bin B (phải >= 1)
//   - requested_threads : Số thread mong muốn sử dụng (phải >= 1)
// Giá trị trả về: Cấu trúc HistogramResult chứa mảng bins, actual_threads và thời gian
// ----------------------------------------------------------------------------
HistogramResult compute_histogram_local(
    const std::vector<uint8_t>& data,
    size_t num_bins,
    int requested_threads
);

// ----------------------------------------------------------------------------
// [Thành viên 2 - Mở rộng tối ưu] Phiên bản Histogram Cục bộ có Padding chống False Sharing
// Cơ chế: Tương tự compute_histogram_local, nhưng căn chỉnh độ dài mỗi hàng cục bộ
//         thành bội số của kích thước cache line (64 bytes = 8 phần tử uint64_t)
//         để 2 thread cạnh nhau không vô tình ghi chung 1 đường cache (false sharing).
// ----------------------------------------------------------------------------
HistogramResult compute_histogram_local_padded(
    const std::vector<uint8_t>& data,
    size_t num_bins,
    int requested_threads
);

// ----------------------------------------------------------------------------
// [Thành viên 2 - Hàm phụ trợ phân tích] Tính lượng bộ nhớ phụ trợ (byte)
// mà thuật toán Histogram Cục bộ tiêu tốn theo công thức O(P * B)
// ----------------------------------------------------------------------------
size_t estimate_local_memory_bytes(
    int num_threads,
    size_t num_bins,
    bool use_padding = false
) noexcept;

// ----------------------------------------------------------------------------
// [Giao diện dự phòng tích hợp với Thành viên 1]
// Khi ghép nhánh với Thành viên 1, các tệp src/histogram_serial.cpp và
// src/histogram_atomic.cpp sẽ định nghĩa hai hàm dưới đây.
// ----------------------------------------------------------------------------
HistogramResult compute_histogram_serial(
    const std::vector<uint8_t>& data,
    size_t num_bins
);

HistogramResult compute_histogram_atomic(
    const std::vector<uint8_t>& data,
    size_t num_bins,
    int requested_threads
);

} // namespace hist
