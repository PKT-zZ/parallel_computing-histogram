#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

// Sinh hai vector uint8_t [0..255] theo seed co dinh de tai lap thi nghiem
void generate_dot_vectors(std::size_t n, uint32_t seed,
                          std::vector<uint8_t>& a,
                          std::vector<uint8_t>& b) {
    a.resize(n);
    b.resize(n);
    std::mt19937 rng(seed);
    for (std::size_t i = 0; i < n; ++i) {
        a[i] = static_cast<uint8_t>(rng() & 0xFFu);
    }
    for (std::size_t i = 0; i < n; ++i) {
        b[i] = static_cast<uint8_t>(rng() & 0xFFu);
    }
}

// Ban tuan tu (baseline kiem chung va moc do T_serial)
// Nhan bang uint32_t (toi da 255*255 = 65025, khong tran) roi cong don vao uint64_t
// de compiler de auto-vectorize hon; dung chi so co dau (long long) de tuong thich ca MSVC OpenMP 2.0.
uint64_t dot_serial(const std::vector<uint8_t>& a,
                    const std::vector<uint8_t>& b) {
    if (a.size() != b.size()) {
        throw std::invalid_argument("Hai vector phai co cung do dai.");
    }

    const long long n = static_cast<long long>(a.size());
    const uint8_t* pa = a.data();
    const uint8_t* pb = b.data();

    uint64_t sum = 0;
    for (long long i = 0; i < n; ++i) {
        sum += static_cast<uint64_t>(
            static_cast<uint32_t>(pa[i]) * static_cast<uint32_t>(pb[i]));
    }
    return sum;
}

// TODO (Tuan 2): Bo sung ham dot_omp(a, b, num_threads, actual_threads)
// su dung #pragma omp parallel num_threads(num_threads) reduction(+:sum),
// lay actual_threads = omp_get_num_threads() trong #pragma omp single,
// va #pragma omp for schedule(static) voi bien vong lap long long.

#ifndef DOT_PRODUCT_NO_MAIN
static unsigned long long parse_strict_ull(const std::string& val, const std::string& flag) {
    if (val.empty()) {
        throw std::invalid_argument("Gia tri rong cho " + flag);
    }
    for (std::size_t i = 0; i < val.size(); ++i) {
        unsigned char ch = static_cast<unsigned char>(val[i]);
        if (!std::isdigit(ch)) {
            throw std::invalid_argument(
                "Gia tri khong hop le (chi chap nhan so nguyen khong am) cho " + flag + ": '" + val + "'");
        }
    }
    try {
        std::size_t pos = 0;
        unsigned long long parsed = std::stoull(val, &pos);
        if (pos != val.size()) {
            throw std::invalid_argument("Gia tri khong phai so nguyen cho " + flag + ": '" + val + "'");
        }
        return parsed;
    } catch (const std::out_of_range&) {
        throw std::invalid_argument("Gia tri vuot gioi han cho " + flag + ": '" + val + "'");
    }
}

static std::size_t parse_size_arg(const std::string& val, const std::string& flag) {
    unsigned long long parsed = parse_strict_ull(val, flag);
    if (parsed > static_cast<unsigned long long>(std::numeric_limits<long long>::max())) {
        throw std::invalid_argument("Gia tri qua lon cho " + flag + ": '" + val + "'");
    }
    return static_cast<std::size_t>(parsed);
}

static uint32_t parse_uint32_arg(const std::string& val, const std::string& flag) {
    unsigned long long parsed = parse_strict_ull(val, flag);
    if (parsed > static_cast<unsigned long long>(std::numeric_limits<uint32_t>::max())) {
        throw std::invalid_argument(
            "Gia tri vuot pham vi uint32_t (0..4294967295) cho " + flag + ": '" + val + "'");
    }
    return static_cast<uint32_t>(parsed);
}

static double compute_median(std::vector<double> values) {
    std::sort(values.begin(), values.end());
    const std::size_t m = values.size();
    if (m % 2 == 1) {
        return values[m / 2];
    }
    return 0.5 * (values[m / 2 - 1] + values[m / 2]);
}

static void print_usage(const char* prog) {
    std::cerr << "Cach dung: " << prog
              << " [--mode serial] [--n <so_phan_tu>] [--seed <seed>]"
              << " [--warmup <so_lan>] [--reps <so_lan>]\n";
}

int main(int argc, char* argv[]) {
    std::string mode = "serial";
    std::size_t n = 1000000;
    uint32_t seed = 42;
    std::size_t warmup = 1;
    std::size_t reps = 5;

    try {
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "--mode" && i + 1 < argc) {
                mode = argv[++i];
            } else if (arg == "--n" && i + 1 < argc) {
                n = parse_size_arg(argv[++i], "--n");
            } else if (arg == "--seed" && i + 1 < argc) {
                seed = parse_uint32_arg(argv[++i], "--seed");
            } else if (arg == "--warmup" && i + 1 < argc) {
                warmup = parse_size_arg(argv[++i], "--warmup");
            } else if (arg == "--reps" && i + 1 < argc) {
                reps = parse_size_arg(argv[++i], "--reps");
            } else if (arg == "--help" || arg == "-h") {
                print_usage(argv[0]);
                return 0;
            } else {
                throw std::invalid_argument("Tham so khong hop le hoac thieu gia tri: " + arg);
            }
        }

        if (mode != "serial") {
            throw std::invalid_argument(
                "Che do chua duoc ho tro o Tuan 1: " + mode + " (chi ho tro 'serial').");
        }
        if (reps == 0) {
            throw std::invalid_argument("Gia tri --reps phai >= 1.");
        }

        // Sinh du lieu truoc khi bam gio theo dung giao thuc do
        std::vector<uint8_t> a, b;
        generate_dot_vectors(n, seed, a, b);

        // Bien volatile chong compiler loai bo hoac gop cac lan goi lap lai o -O3
        volatile uint64_t sink = 0;

        // Chay warm-up truoc cac lan do chinh thuc
        for (std::size_t w = 0; w < warmup; ++w) {
            sink = dot_serial(a, b);
        }

        // Do chinh thuc 'reps' lan va in tung lan de ghi raw.csv
        std::vector<double> times_ms;
        times_ms.reserve(reps);
        uint64_t final_result = 0;

        for (std::size_t r = 1; r <= reps; ++r) {
            auto t_start = std::chrono::steady_clock::now();
            uint64_t result = dot_serial(a, b);
            sink = result;
            auto t_end = std::chrono::steady_clock::now();

            if (r == 1) {
                final_result = result;
            } else if (result != final_result) {
                throw std::runtime_error("Ket qua khong on dinh giua cac lan lap.");
            }

            double elapsed_ms =
                std::chrono::duration<double, std::milli>(t_end - t_start).count();
            times_ms.push_back(elapsed_ms);

            std::cout << "run=" << r
                      << " mode=" << mode
                      << " n=" << n
                      << " seed=" << seed
                      << " threads=1"
                      << " result=" << result
                      << " time_ms=" << std::fixed << std::setprecision(6) << elapsed_ms
                      << "\n";
        }
        (void)sink;

        // Dung pair thay cho structured binding de tuong thich tu C++11 tro len
        const auto mm = std::minmax_element(times_ms.begin(), times_ms.end());
        const double median_ms = compute_median(times_ms);

        std::cout << "summary"
                  << " mode=" << mode
                  << " n=" << n
                  << " seed=" << seed
                  << " threads=1"
                  << " warmup=" << warmup
                  << " reps=" << reps
                  << " result=" << final_result
                  << " median_ms=" << std::fixed << std::setprecision(6) << median_ms
                  << " min_ms=" << *mm.first
                  << " max_ms=" << *mm.second
                  << "\n";
        return 0;
    } catch (const std::invalid_argument& ex) {
        std::cerr << "Loi tham so: " << ex.what() << "\n";
        print_usage(argv[0]);
        return 1;
    } catch (const std::exception& ex) {
        std::cerr << "Loi thuc thi: " << ex.what() << "\n";
        return 1;
    }
}
#endif
