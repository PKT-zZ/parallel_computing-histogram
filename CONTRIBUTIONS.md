# Đóng góp của từng thành viên (CONTRIBUTIONS)

> **Người tổng hợp**: Thành viên 2 (tổng hợp từ lịch sử commit Git và đầu việc thực tế của cả nhóm; cập nhật đầy đủ tên, MSSV, tài khoản Git và mã commit trước khi nộp).

## 1. Bảng phân công và bàn giao sản phẩm

| Thành viên | Họ tên / MSSV / Tài khoản Git | Mã nguồn & Kiểm thử | Thực nghiệm & Công cụ | Báo cáo & Slide | Review chéo đã thực hiện |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **Thành viên 1** *(Nền tảng Histogram & Atomic)* | *(Điền Họ tên, MSSV, @git_user)* | `src/histogram_serial.cpp`, `src/histogram_atomic.cpp`, `src/generate_data.cpp`, `tests/test_histogram.cpp` | Chạy benchmark Nhóm A (3 kích thước $N$, uniform/skewed); tổng hợp `AI_USAGE.md` | PCAM histogram, cơ chế race & atomic trong báo cáo + slide | Review mã & test của Thành viên 2 / 3 *(ghi PR/commit)* |
| **Thành viên 2** *(Histogram Cục bộ & Phân tích Hiệu năng)* | *(Điền Họ tên, MSSV, @git_user)* | `include/histogram.hpp`, `src/histogram_local.cpp`, `tests/test_histogram_local.cpp` | `CMakeLists.txt`, `scripts/build.ps1`, `scripts/build.sh`; chạy benchmark Nhóm B (số bin $B=16,64,256$); tổng hợp biểu đồ histogram & `CONTRIBUTIONS.md` | Thiết kế cục bộ & gộp, bảng bộ nhớ $O(P \times B)$, phân tích biểu đồ histogram, giải thích speedup thấp/dưới 1, giới hạn thí nghiệm + slide | Review mã & test của Thành viên 1 / 3 *(ghi PR/commit)* |
| **Thành viên 3** *(Bài chung & Pipeline Thí nghiệm)* | *(Điền Họ tên, MSSV, @git_user)* | `src/dot_product.cpp`, `tests/test_dot.cpp` | `scripts/run_bench.py`, `scripts/plot_results.py`, `results/raw.csv`, `results/summary.csv`, `README.md`; chạy benchmark Nhóm C (tích vô hướng); tổng hợp biểu đồ tích vô hướng | Phương pháp đo, môi trường, kết quả bài chung tích vô hướng + slide | Review mã & test của Thành viên 1 / 2 *(ghi PR/commit)* |

---

## 2. Lịch sử mốc commit thực tế (Cập nhật từ `git log --stat`)

- **Tuần 1**: Khởi tạo kho Git, chốt giao diện `include/histogram.hpp`, PCAM nháp, bản baseline tuần tự và test mẫu đầu tiên.
- **Tuần 2**: Hoàn thiện `histogram_atomic.cpp`, `histogram_local.cpp`, `generate_data.cpp`, `dot_product.cpp`, `CMakeLists.txt` và toàn bộ bộ test đối chiếu; review chéo.
- **Tuần 3**: Hoàn thiện `run_bench.py`, `plot_results.py`, chạy pilot chốt 3 kích thước $N$ và mức thread $P$.
- **Tuần 4**: Chạy thí nghiệm chính thức ($\ge 5$ lần/cấu hình) theo lịch tuần tự trên cùng máy đo; xuất `raw.csv`, `summary.csv` và biểu đồ.
- **Tuần 5**: Hoàn thiện `README.md`, báo cáo, slide, `AI_USAGE.md`, `CONTRIBUTIONS.md`; kiểm thử chạy lại từ đầu trên máy sạch và diễn tập bảo vệ.

---

## 3. Xác nhận của nhóm

Cả ba thành viên xác nhận mọi đóng góp trên phản ánh đúng quá trình làm việc thực tế trên kho Git (dùng tài khoản riêng, không gộp toàn bộ vào 1–2 commit cuối) và từng thành viên nắm rõ luồng chung cũng như toàn bộ dòng mã thuộc phần mình phụ trách.
