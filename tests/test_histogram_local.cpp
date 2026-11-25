// ============================================================================
// Tệp: tests/test_histogram_local.cpp
// Tác giả: Thành viên 2 - Histogram cục bộ và phân tích hiệu năng
// Mô tả: Bộ kiểm thử đối chiếu độc lập cho phiên bản Histogram Cục bộ & Gộp.
//        Kiểm tra tính đúng đắn từng bin, tổng bin == N, các trường hợp biên,
//        N không chia hết cho p, dữ liệu uniform/skewed, lặp nhiều lần cùng seed,
//        kiểm tra số thread thực tế (actual_threads) và xử lý lỗi đầu vào.
// ============================================================================

// Nạp giao diện các hàm histogram cục bộ cần kiểm thử
#include "histogram.hpp"

// Nạp thư viện OpenMP để lấy thông tin số CPU tối đa của máy (omp_get_max_threads)
#include <omp.h>
// Nạp thư viện vào/ra chuẩn để in báo cáo kết quả từng ca kiểm thử
#include <iostream>
// Nạp thư viện random với bộ sinh số giả ngẫu nhiên Mersenne Twister (std::mt19937)
#include <random>
// Nạp thư viện numeric để dùng std::accumulate tính tổng các bin
#include <numeric>
// Nạp thư viện string để đặt tên mô tả cho từng ca kiểm thử
#include <string>
// Nạp thư viện stdexcept để kiểm tra việc ném ngoại lệ khi đầu vào không hợp lệ
#include <stdexcept>
// Nạp thư viện vector để lưu danh sách cấu hình test
#include <vector>

// ----------------------------------------------------------------------------
// Hàm chuẩn tuần tự ĐỘC LẬP dùng làm mốc (Oracle) để đối chiếu kết quả
// Theo mục 3.1: Mã kiểm thử cần độc lập với mã song song để phát hiện lỗi
// ánh xạ bin, mất cập nhật, vượt chỉ số mảng hoặc tràn kiểu tích lũy.
// ----------------------------------------------------------------------------
static std::vector<uint64_t> reference_histogram_serial(
    const std::vector<uint8_t>& data,
    size_t num_bins
) {
    // Khởi tạo mảng chuẩn gồm num_bins phần tử có giá trị ban đầu bằng 0
    std::vector<uint64_t> ref(num_bins, 0ULL);
    // Duyệt tuần tự qua từng phần tử x trong mảng đầu vào
    for (uint8_t x : data) {
        // Tính trực tiếp công thức toán học floor(x * B / 256) bằng phép chia nguyên
        size_t b = (static_cast<size_t>(x) * num_bins) / 256ULL;
        // Tăng bộ đếm tại bin b thêm 1
        ref[b] += 1ULL;
    }
    // Trả về mảng histogram chuẩn
    return ref;
}

// ----------------------------------------------------------------------------
// Hàm sinh dữ liệu phân phối đều (Uniform) trên đoạn [0, 255] với seed cố định
// ----------------------------------------------------------------------------
static std::vector<uint8_t> make_uniform_data(size_t n, uint32_t seed) {
    // Khởi tạo bộ sinh số ngẫu nhiên mt19937 với hạt giống seed cố định để tái lập được
    std::mt19937 rng(seed);
    // Phân phối nguyên đều từ 0 đến 255
    std::uniform_int_distribution<int> dist(0, 255);
    // Cấp phát sẵn mảng kích thước n phần tử
    std::vector<uint8_t> data(n);
    // Gán từng phần tử ngẫu nhiên vào mảng
    for (size_t i = 0; i < n; ++i) {
        data[i] = static_cast<uint8_t>(dist(rng));
    }
    // Trả về mảng dữ liệu uniform
    return data;
}

// ----------------------------------------------------------------------------
// Hàm sinh dữ liệu phân phối lệch (Skewed) theo đặc tả mục 2.1:
// Khoảng 90% phần tử có giá trị bằng 0 (bin nóng), 10% còn lại rải đều trên [0, 255]
// ----------------------------------------------------------------------------
static std::vector<uint8_t> make_skewed_data(size_t n, uint32_t seed) {
    // Khởi tạo bộ sinh số ngẫu nhiên với hạt giống seed cố định
    std::mt19937 rng(seed);
    // Phân phối xác suất thực trong [0.0, 1.0) để quyết định 90% hay 10%
    std::uniform_real_distribution<double> prob(0.0, 1.0);
    // Phân phối đều [0, 255] dùng cho 10% phần tử không bị ép về 0
    std::uniform_int_distribution<int> val_dist(0, 255);
    // Cấp phát mảng kết quả kích thước n
    std::vector<uint8_t> data(n);
    // Sinh từng phần tử theo tỷ lệ 90% bằng 0, 10% rải đều
    for (size_t i = 0; i < n; ++i) {
        if (prob(rng) < 0.90) {
            data[i] = 0;
        } else {
            data[i] = static_cast<uint8_t>(val_dist(rng));
        }
    }
    // Trả về mảng dữ liệu skewed
    return data;
}

// Biến đếm tổng số ca kiểm thử đã chạy và số ca thất bại
static int g_total_tests = 0;
static int g_failed_tests = 0;

// ----------------------------------------------------------------------------
// Hàm trợ giúp đối chiếu 1 cấu hình (data, B, requested_threads) cho cả hai
// phiên bản: bản chuẩn (local) và bản có padding (local_padded)
// ----------------------------------------------------------------------------
static void verify_case(
    const std::string& case_name,
    const std::vector<uint8_t>& data,
    size_t num_bins,
    int requested_threads
) {
    // Tăng tổng số ca kiểm thử đã thực hiện
    ++g_total_tests;

    // Tính mảng histogram chuẩn tuần tự làm mốc so sánh
    const std::vector<uint64_t> expected = reference_histogram_serial(data, num_bins);

    // Chạy lần lượt cả 2 phiên bản: false = bản chuẩn, true = bản có padding
    for (int mode = 0; mode < 2; ++mode) {
        // Đặt nhãn phân biệt bản chuẩn và bản có padding khi in lỗi (nếu có)
        const std::string mode_name = (mode == 0) ? "local" : "local_padded";

        // Gọi hàm cần kiểm thử tương ứng
        hist::HistogramResult res = (mode == 0)
            ? hist::compute_histogram_local(data, num_bins, requested_threads)
            : hist::compute_histogram_local_padded(data, num_bins, requested_threads);

        // Kiểm tra 1: Số lượng bin trả về phải đúng bằng num_bins yêu cầu
        if (res.bins.size() != num_bins) {
            std::cerr << "[FAIL] " << case_name << " (" << mode_name
                      << "): Sai kich thuoc mang bin. Ky vong=" << num_bins
                      << ", Thuc te=" << res.bins.size() << "\n";
            ++g_failed_tests;
            return;
        }

        // Kiểm tra 2: Tổng tất cả các bin phải đúng bằng số phần tử N của dữ liệu
        const uint64_t total_count = std::accumulate(res.bins.begin(), res.bins.end(), 0ULL);
        if (total_count != static_cast<uint64_t>(data.size())) {
            std::cerr << "[FAIL] " << case_name << " (" << mode_name
                      << "): Tong bin khac N. Tong=" << total_count
                      << ", N=" << data.size() << "\n";
            ++g_failed_tests;
            return;
        }

        // Kiểm tra 3: Đối chiếu từng bin một với kết quả tuần tự chuẩn
        for (size_t b = 0; b < num_bins; ++b) {
            if (res.bins[b] != expected[b]) {
                std::cerr << "[FAIL] " << case_name << " (" << mode_name
                          << "): Sai lech tai bin " << b
                          << ". Chuan=" << expected[b]
                          << ", Song song=" << res.bins[b] << "\n";
                ++g_failed_tests;
                return;
            }
        }

        // Kiểm tra 4: Số thread thực tế phải hợp lệ (1 <= actual_threads <= requested_threads)
        if (res.actual_threads < 1 || res.actual_threads > requested_threads) {
            std::cerr << "[FAIL] " << case_name << " (" << mode_name
                      << "): actual_threads khong hop le. Yeu cau=" << requested_threads
                      << ", Thuc te=" << res.actual_threads << "\n";
            ++g_failed_tests;
            return;
        }

        // Kiểm tra 5: Thời gian đo được phải không âm
        if (res.elapsed_seconds < 0.0) {
            std::cerr << "[FAIL] " << case_name << " (" << mode_name
                      << "): Thoi gian thuc thi am (" << res.elapsed_seconds << ")\n";
            ++g_failed_tests;
            return;
        }
    }
}

// ----------------------------------------------------------------------------
// Nhóm kiểm thử 1: Các trường hợp biên cơ bản (N=0, N=1, giá trị 0 và 255, toàn bộ giống nhau)
// ----------------------------------------------------------------------------
static void test_edge_cases() {
    std::cout << "[1/6] Dang chay test truong hop bien (N=0, N=1, bien 0/255, dong nhat)...\n";

    // Danh sách số bin bắt buộc theo kế hoạch: 16, 64, 256
    const std::vector<size_t> bin_list = {16, 64, 256};
    // Danh sách số thread kiểm thử
    const std::vector<int> thread_list = {1, 2, 4};

    // Trường hợp N = 0 (mảng rỗng)
    const std::vector<uint8_t> empty_data;
    for (size_t b : bin_list) {
        for (int p : thread_list) {
            verify_case("N=0_B=" + std::to_string(b) + "_p=" + std::to_string(p), empty_data, b, p);
        }
    }

    // Trường hợp N = 1 với giá trị biên 0, giữa 128 và biên trên 255
    const std::vector<uint8_t> single_vals = {0, 128, 255};
    for (uint8_t val : single_vals) {
        std::vector<uint8_t> one_elem = {val};
        for (size_t b : bin_list) {
            for (int p : thread_list) {
                verify_case(
                    "N=1_val=" + std::to_string(val) + "_B=" + std::to_string(b) + "_p=" + std::to_string(p),
                    one_elem, b, p
                );
            }
        }
    }

    // Trường hợp toàn bộ phần tử giống nhau (gây dồn toàn bộ vào 1 bin duy nhất)
    const size_t n_same = 50000;
    for (uint8_t val : single_vals) {
        std::vector<uint8_t> all_same(n_same, val);
        for (size_t b : bin_list) {
            verify_case("AllSame_val=" + std::to_string(val) + "_B=" + std::to_string(b), all_same, b, 4);
        }
    }
}

// ----------------------------------------------------------------------------
// Nhóm kiểm thử 2: N không chia hết cho số thread p (kiểm tra chia block static)
// ----------------------------------------------------------------------------
static void test_non_divisible_n() {
    std::cout << "[2/6] Dang chay test N khong chia het cho so thread p...\n";

    // Chọn N là các số nguyên tố hoặc số lẻ không chia hết cho 2, 3, 4, 7, 8
    const std::vector<size_t> sizes = {7, 13, 1009, 100003};
    // Thử nhiều mức thread khác nhau kể cả số lẻ (3, 7)
    const std::vector<int> thread_counts = {1, 2, 3, 4, 7, 8};
    const std::vector<size_t> bin_list = {16, 64, 256};

    for (size_t n : sizes) {
        // Sinh dữ liệu uniform bằng seed cố định 42
        std::vector<uint8_t> data = make_uniform_data(n, 42U);
        for (size_t b : bin_list) {
            for (int p : thread_counts) {
                verify_case(
                    "NonDiv_N=" + std::to_string(n) + "_B=" + std::to_string(b) + "_p=" + std::to_string(p),
                    data, b, p
                );
            }
        }
    }
}

// ----------------------------------------------------------------------------
// Nhóm kiểm thử 3: Dữ liệu phân phối lệch (Skewed: 90% giá trị 0) và đều (Uniform)
// ----------------------------------------------------------------------------
static void test_distributions() {
    std::cout << "[3/6] Dang chay test phan phoi Uniform va Skewed (90% = 0)...\n";

    // Chọn kích thước N đủ lớn để chia nhiều khối cho các thread và không chia hết cho 4/8
    const size_t n = 250007;
    // Sinh hai bộ dữ liệu với seed cố định 2026
    const std::vector<uint8_t> uniform_data = make_uniform_data(n, 2026U);
    const std::vector<uint8_t> skewed_data = make_skewed_data(n, 2026U);

    const std::vector<size_t> bin_list = {16, 64, 256};
    const std::vector<int> thread_counts = {1, 2, 4, 8};

    for (size_t b : bin_list) {
        for (int p : thread_counts) {
            verify_case("Uniform_B=" + std::to_string(b) + "_p=" + std::to_string(p), uniform_data, b, p);
            verify_case("Skewed_B=" + std::to_string(b) + "_p=" + std::to_string(p), skewed_data, b, p);
        }
    }
}

// ----------------------------------------------------------------------------
// Nhóm kiểm thử 4: Lặp nhiều lần với cùng seed để kiểm tra tính ổn định (không race)
// ----------------------------------------------------------------------------
static void test_repeatability_same_seed() {
    std::cout << "[4/6] Dang chay test lap 5 lan cung seed (kiem tra khong data race)...\n";

    // Sinh dữ liệu skewed với seed cố định 999
    const std::vector<uint8_t> data = make_skewed_data(300001, 999U);
    // Lấy số thread tối đa khả dụng trên máy
    const int max_p = omp_get_max_threads();
    // Chạy lần đầu làm chuẩn đối chiếu tính nhất quán giữa các lần chạy
    const hist::HistogramResult baseline = hist::compute_histogram_local(data, 256, max_p);

    // Chạy lặp lại 5 lần liên tiếp và so sánh từng bin với lần đầu
    for (int iter = 1; iter <= 5; ++iter) {
        ++g_total_tests;
        const hist::HistogramResult cur = hist::compute_histogram_local(data, 256, max_p);
        if (cur.bins != baseline.bins) {
            std::cerr << "[FAIL] Repeatability lan " << iter << ": Ket qua thay doi giua cac lan chay!\n";
            ++g_failed_tests;
            return;
        }
    }
}

// ----------------------------------------------------------------------------
// Nhóm kiểm thử 5: Kiểm tra ghi nhận số thread thực tế (actual_threads)
// và kiểm tra công thức ước lượng bộ nhớ phụ trợ O(P * B)
// ----------------------------------------------------------------------------
static void test_actual_threads_and_memory() {
    std::cout << "[5/6] Dang chay test kiem tra actual_threads va bo nho O(P*B)...\n";

    // Tạo mảng mẫu nhỏ
    const std::vector<uint8_t> data = make_uniform_data(10000, 7U);
    // Lấy số thread mặc định tối đa mà OpenMP hỗ trợ trên máy hiện tại
    const int max_hw_threads = omp_get_max_threads();

    // Kiểm tra yêu cầu 1 thread -> thực tế bắt buộc phải là 1
    ++g_total_tests;
    hist::HistogramResult res1 = hist::compute_histogram_local(data, 64, 1);
    if (res1.actual_threads != 1) {
        std::cerr << "[FAIL] Yeu cau 1 thread nhung actual_threads=" << res1.actual_threads << "\n";
        ++g_failed_tests;
    }

    // Kiểm tra yêu cầu đúng max_hw_threads và in thông tin để xác minh bằng mắt
    ++g_total_tests;
    hist::HistogramResult res_max = hist::compute_histogram_local(data, 256, max_hw_threads);
    std::cout << "      -> Thong tin OpenMP: requested=" << max_hw_threads
              << ", actual_threads=" << res_max.actual_threads << "\n";
    if (res_max.actual_threads < 1 || res_max.actual_threads > max_hw_threads) {
        std::cerr << "[FAIL] actual_threads nam ngoai khoang [1, " << max_hw_threads << "]\n";
        ++g_failed_tests;
    }

    // Kiểm tra công thức bộ nhớ phụ trợ O(P * B) * 8 byte
    ++g_total_tests;
    const size_t mem_no_pad = hist::estimate_local_memory_bytes(4, 16, false);
    const size_t expected_no_pad = 4ULL * 16ULL * sizeof(uint64_t); // 512 bytes
    if (mem_no_pad != expected_no_pad) {
        std::cerr << "[FAIL] Sai uoc luong bo nho khong padding: " << mem_no_pad << "\n";
        ++g_failed_tests;
    }

    // Kiểm tra công thức bộ nhớ khi có padding với B=10 (làm tròn lên bội của 8 -> 16)
    ++g_total_tests;
    const size_t mem_pad = hist::estimate_local_memory_bytes(4, 10, true);
    const size_t expected_pad = 4ULL * 16ULL * sizeof(uint64_t); // 512 bytes
    if (mem_pad != expected_pad) {
        std::cerr << "[FAIL] Sai uoc luong bo nho co padding: " << mem_pad << "\n";
        ++g_failed_tests;
    }
}

// ----------------------------------------------------------------------------
// Nhóm kiểm thử 6: Kiểm tra xử lý lỗi khi đầu vào không hợp lệ (B=0 hoặc p<=0)
// ----------------------------------------------------------------------------
static void test_invalid_inputs() {
    std::cout << "[6/6] Dang chay test bat loi dau vao (B=0, requested_threads<=0)...\n";

    const std::vector<uint8_t> data = {1, 2, 3};

    // Kiểm tra B = 0 phải ném std::invalid_argument
    ++g_total_tests;
    bool caught_b0 = false;
    try {
        (void)hist::compute_histogram_local(data, 0, 2);
    } catch (const std::invalid_argument&) {
        caught_b0 = true;
    }
    if (!caught_b0) {
        std::cerr << "[FAIL] Khong nem ngoai le khi num_bins == 0!\n";
        ++g_failed_tests;
    }

    // Kiểm tra requested_threads = 0 phải ném std::invalid_argument
    ++g_total_tests;
    bool caught_p0 = false;
    try {
        (void)hist::compute_histogram_local(data, 16, 0);
    } catch (const std::invalid_argument&) {
        caught_p0 = true;
    }
    if (!caught_p0) {
        std::cerr << "[FAIL] Khong nem ngoai le khi requested_threads <= 0!\n";
        ++g_failed_tests;
    }
}

// ----------------------------------------------------------------------------
// Hàm main điều phối chạy toàn bộ 6 nhóm kiểm thử của Thành viên 2
// Trả về 0 nếu mọi test PASS, trả về 1 nếu có bất kỳ test nào FAIL
// ----------------------------------------------------------------------------
int main() {
    std::cout << "============================================================\n";
    std::cout << "  BO KIEM THU THANH VIEN 2: HISTOGRAM CUC BO & GOP (OpenMP) \n";
    std::cout << "============================================================\n";

    // Lần lượt chạy từng nhóm kiểm thử
    test_edge_cases();
    test_non_divisible_n();
    test_distributions();
    test_repeatability_same_seed();
    test_actual_threads_and_memory();
    test_invalid_inputs();

    // In tổng kết số lượng test đạt / lỗi
    std::cout << "------------------------------------------------------------\n";
    std::cout << "Tong so ca kiem thu: " << g_total_tests
              << " | Dat: " << (g_total_tests - g_failed_tests)
              << " | Loi: " << g_failed_tests << "\n";
    std::cout << "============================================================\n";

    // Trả về mã thoát chuẩn để CTest và CI/CD nhận diện kết quả
    return (g_failed_tests == 0) ? 0 : 1;
}
