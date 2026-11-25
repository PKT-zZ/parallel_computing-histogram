#include <cstddef>
#include <cstdint>
#include <functional>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>

// Khai bao ham tu src/dot_product.cpp (khong can file header rieng)
void generate_dot_vectors(std::size_t n, uint32_t seed,
                          std::vector<uint8_t>& a,
                          std::vector<uint8_t>& b);
uint64_t dot_serial(const std::vector<uint8_t>& a,
                    const std::vector<uint8_t>& b);

static int g_failed = 0;

static void check_eq(const std::string& name, uint64_t actual, uint64_t expected) {
    if (actual == expected) {
        std::cout << "[PASS] " << name << " (ket qua = " << actual << ")\n";
    } else {
        std::cerr << "[FAIL] " << name << " | thuc te = " << actual
                  << ", ky vong = " << expected << "\n";
        ++g_failed;
    }
}

static void check_true(const std::string& name, bool cond) {
    if (cond) {
        std::cout << "[PASS] " << name << "\n";
    } else {
        std::cerr << "[FAIL] " << name << "\n";
        ++g_failed;
    }
}

int main() {
    // 1. Mau nho tinh tay: 1*4 + 2*5 + 3*6 = 32
    {
        std::vector<uint8_t> a = {1, 2, 3};
        std::vector<uint8_t> b = {4, 5, 6};
        check_eq("Mau nho tinh tay (N=3)", dot_serial(a, b), 32ULL);
    }

    // 2. Truong hop bien N = 0 (ca generate_dot_vectors lan dot_serial)
    {
        std::vector<uint8_t> a = {1, 2}, b = {3, 4};
        generate_dot_vectors(0, 42, a, b);
        check_true("generate_dot_vectors(0) cho kich thuoc a.size() == b.size() == 0",
                   a.empty() && b.empty());
        check_eq("Truong hop bien N=0", dot_serial(a, b), 0ULL);
    }

    // 3. Truong hop bien N = 1 voi gia tri lon nhat 255
    {
        std::vector<uint8_t> a = {255};
        std::vector<uint8_t> b = {255};
        check_eq("Truong hop bien N=1 (255*255)", dot_serial(a, b), 65025ULL);
    }

    // 4. Toan bo gia tri bang 0
    {
        std::vector<uint8_t> a(1000, 0);
        std::vector<uint8_t> b(1000, 255);
        check_eq("Gia tri bien toan 0", dot_serial(a, b), 0ULL);
    }

    // 5. Kiem tra khong tran 32-bit: 100000 * 255 * 255 = 6,502,500,000 (> UINT32_MAX)
    {
        const std::size_t n = 100000;
        std::vector<uint8_t> a(n, 255);
        std::vector<uint8_t> b(n, 255);
        check_eq("Kiem tra tich luy uint64_t (khong tran 32-bit)",
                 dot_serial(a, b), 6502500000ULL);
    }

    // 6. Kiem tra tinh tai lap truc tiep tren du lieu sinh cung seed (a1 == a2 && b1 == b2)
    {
        std::vector<uint8_t> a1, b1, a2, b2;
        generate_dot_vectors(1024, 12345, a1, b1);
        generate_dot_vectors(1024, 12345, a2, b2);
        check_true("Kich thuoc du lieu sinh dung (a.size() == b.size() == 1024)",
                   a1.size() == 1024 && b1.size() == 1024);
        check_true("Tai lap du lieu cung seed (a1 == a2 && b1 == b2)",
                   a1 == a2 && b1 == b2);
        check_eq("Tai lap ket qua cung seed (N=1024, seed=12345)",
                 dot_serial(a1, b1), dot_serial(a2, b2));
    }

    // 7. Golden value co dinh cho (n=1000, seed=42) - phat hien neu thay doi bo sinh du lieu
    {
        std::vector<uint8_t> a, b;
        generate_dot_vectors(1000, 42, a, b);
        check_eq("Golden value (N=1000, seed=42)", dot_serial(a, b), 16699833ULL);
    }

    // 8. Doi chieu du lieu ngau nhien voi ban tham chieu doc lap std::inner_product (uint64_t)
    // gom cac kich thuoc quanh bien do rong SIMD (16/32/64 byte) de bat loi phan duoi
    {
        const std::vector<std::size_t> test_sizes = {
            7, 15, 16, 17, 31, 32, 33, 63, 64, 65, 1009, 100003
        };
        for (std::size_t n : test_sizes) {
            std::vector<uint8_t> a, b;
            generate_dot_vectors(n, 2026, a, b);
            if (a.size() != n || b.size() != n) {
                check_true("Kich thuoc vector sau khi sinh (N=" + std::to_string(n) + ")", false);
                continue;
            }

            uint64_t ref = std::inner_product(
                a.begin(), a.end(), b.begin(), uint64_t{0},
                std::plus<uint64_t>{},
                [](uint8_t x, uint8_t y) -> uint64_t {
                    return static_cast<uint64_t>(x) * static_cast<uint64_t>(y);
                });

            check_eq("Doi chieu std::inner_product (N=" + std::to_string(n) + ")",
                     dot_serial(a, b), ref);
        }
    }

    // 9. Kiem tra bao loi khi 2 vector khac do dai
    {
        bool threw = false;
        try {
            std::vector<uint8_t> a = {1, 2};
            std::vector<uint8_t> b = {1};
            (void)dot_serial(a, b);
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        check_true("Bao loi khi khac do dai vector", threw);
    }

    // TODO (Tuan 2): Bo sung test doi chieu dot_omp voi dot_serial
    // (N=1, N=7, N khong chia het cho so thread p, nhieu muc p, lap cung seed).

    if (g_failed == 0) {
        std::cout << "Tat ca test Tuan 1 da PASS.\n";
        return 0;
    }
    std::cerr << "Co " << g_failed << " test bi FAIL.\n";
    return 1;
}
