# Dàn ý Slide Trình bày — Phần Thành viên 2
**Chủ đề 4: Histogram song song cho dữ liệu lớn (Phiên bản Cục bộ & Gộp + Phân tích Hiệu năng)**

---

## Slide 1: Ý tưởng Histogram Cục bộ & Gộp (Local Histogram + Reduction)
- **Vấn đề của bản Atomic**: Nhiều thread cùng cập nhật một mảng `H[]` chung $\rightarrow$ tranh chấp (contention), đặc biệt nghiêm trọng khi dữ liệu lệch (90% rơi vào `H[0]`).
- **Giải pháp Cục bộ & Gộp**:
  1. **Pha đếm song song**: Cấp phát mảng phẳng `local_hists` kích thước $P \times B$. Mỗi thread `tid` chỉ tăng bộ đếm trên vùng riêng `[tid*B ... tid*B + B - 1]`. $\rightarrow$ **0 tranh chấp, 0 data race**.
  2. **Đồng bộ (Implicit Barrier)**: Cuối `#pragma omp for` / `#pragma omp parallel` đảm bảo 100% thread đếm xong.
  3. **Pha gộp (Reduction)**: Cộng dồn tuần tự từng bin từ $P$ mảng cục bộ về mảng kết quả `result.bins`.

---

## Slide 2: Cài đặt OpenMP & Kiểm soát bộ nhớ $O(P \times B)$
- **Tổ chức bộ nhớ**: Dùng 1 `std::vector<uint64_t>` phẳng liên tiếp thay vì vector 2 chiều $\rightarrow$ tránh phân mảnh heap, tăng cache locality.
- **Lập lịch**: `#pragma omp for schedule(static)` vì chi phí xử lý mỗi phần tử `uint8_t` là hằng số $O(1)$.
- **Ghi nhận số thread thực tế**: Đọc `omp_get_num_threads()` bên trong `#pragma omp single` (không chỉ dùng số thread yêu cầu).
- **Dung lượng phụ trợ** (`uint64_t` = 8 byte):
  - $B = 16, P = 8 \Rightarrow 1\text{ KB}$
  - $B = 64, P = 8 \Rightarrow 4\text{ KB}$
  - $B = 256, P = 8 \Rightarrow 16\text{ KB}$ (mỗi thread chỉ dùng 2 KB, nằm gọn trong L1 cache).
- **Mở rộng chống False Sharing**: Căn chỉnh `stride` theo bội số cache line 64 byte (8 phần tử `uint64_t`).

---

## Slide 3: Hệ thống Build, Tích hợp & Kiểm thử đối chiếu
- **Build nhất quán**: `CMakeLists.txt` (C++17, `OpenMP::OpenMP_CXX`, cờ tối ưu `-O3` / `/O2`, cảnh báo `-Wall -Wextra`, tự động tích hợp mã nguồn TV1 & TV3 và chạy qua `CTest`).
- **Bộ kiểm thử độc lập (`test_histogram_local.cpp`)**:
  - Đối chiếu từng bin với hàm tuần tự độc lập; kiểm tra $\sum H[b] = N$.
  - Phủ đủ biên: $N = 0, N = 1$, giá trị biên $0$ và $255$, toàn bộ phần tử giống nhau.
  - $N$ không chia hết cho $p$ (số nguyên tố `7, 13, 1009, 100003` với $p = 1, 2, 3, 4, 7, 8$).
  - Dữ liệu Uniform & Skewed (90% = 0) với $B \in \{16, 64, 256\}$, lặp 5 lần cùng seed, kiểm tra `actual_threads` và bắt lỗi đầu vào ($B = 0, p \le 0$).

---

## Slide 4: Phân tích Hiệu năng — Nhóm A & Nhóm B
- *(Chèn biểu đồ Speedup & Efficiency xuất từ `results/summary.csv` thật cho Nhóm A và Nhóm B — ghi rõ trục, đơn vị, $N, B$, phân phối, $p$, phạm vi đồng hồ $T_{\text{total}}$)*.
- **Điểm nổi bật cần nhấn mạnh khi thuyết trình**:
  - Trên dữ liệu **Skewed**, bản Local vượt trội hoàn toàn so với bản Atomic (không bị cache-line bouncing tại `H[0]`).
  - Khi thay đổi số bin $B \in \{16, 64, 256\}$, bản Local giữ hiệu năng ổn định do chi phí gộp $O(P \cdot B)$ rất nhỏ so với $N$.

---

## Slide 5: Giải thích Speedup thấp / dưới 1 & Giới hạn thí nghiệm
- **Vì sao có thể gặp $S_p < 1$ hoặc $S_p$ bão hòa sớm?**
  1. $N$ nhỏ: chi phí fork-join, cấp phát/khởi tạo $O(P \cdot B)$ và gộp lớn hơn thời gian đếm.
  2. $N$ lớn (vượt L3 cache): bài toán là **memory-bound** (cường độ tính toán thấp), nghẽn băng thông RAM.
  3. $p$ vượt số lõi vật lý (SMT/Hyper-Threading): tranh chấp đơn vị thực thi & cache L1/L2.
- **Giới hạn thí nghiệm**: Đo trên 1 máy CPU shared-memory, chịu ảnh hưởng nhẹ từ Turbo Boost/tiến trình nền (đã giảm thiểu bằng warm-up + 5 lần đo + median/IQR).
