# Báo cáo Phần Thành viên 2: Histogram Cục bộ & Gộp (Local + Reduction) và Phân tích Hiệu năng

> **Phạm vi bàn giao của Thành viên 2** (Chủ đề 4 — Histogram song song cho dữ liệu lớn):
> 1. Cài đặt thuật toán Histogram cục bộ và gộp (`src/histogram_local.cpp`, `include/histogram.hpp`).
> 2. Bộ kiểm thử đối chiếu độc lập (`tests/test_histogram_local.cpp`).
> 3. Hệ thống build và tích hợp nhóm (`CMakeLists.txt`, `scripts/build.ps1`, `scripts/build.sh`).
> 4. Phân tích chi phí bộ nhớ $O(P \times B)$, hiệu năng, trường hợp speedup thấp / dưới 1 và giới hạn thí nghiệm.

---

## 1. Thiết kế thuật toán Histogram Cục bộ và Gộp theo quy trình PCAM

Bài toán yêu cầu tính mảng tần suất $H[0 \dots B-1]$ cho mảng đầu vào gồm $N$ phần tử `uint8_t` ($x \in [0, 255]$) với quy tắc ánh xạ:

$$\text{bin}(x) = \left\lfloor \frac{x \times B}{256} \right\rfloor, \quad B \in \{16, 64, 256\}, \quad \sum_{b=0}^{B-1} H[b] = N$$

Áp dụng 4 bước PCAM cho phương án **Histogram cục bộ và gộp**:

| Bước PCAM | Quyết định thiết kế cho phiên bản Cục bộ & Gộp | Lý do kỹ thuật |
| :--- | :--- | :--- |
| **Partitioning** (Phân hoạch) | Chia mảng đầu vào kích thước $N$ thành các đoạn liên tiếp; mỗi phần tử thuộc đúng một thread. Mỗi thread giữ một mảng đếm cục bộ riêng kích thước $B$. | Các phần tử đầu vào độc lập với nhau; tách mảng đếm giúp loại bỏ hoàn toàn phụ thuộc ghi (write-after-write hazard) trong pha quét dữ liệu. |
| **Communication** (Giao tiếp) | **Không có giao tiếp** trong suốt quá trình quét $N$ phần tử. Giao tiếp chỉ xảy ra **một lần duy nhất** ở pha gộp (reduction) sau khi mọi thread đã hoàn tất đếm. | Loại bỏ hoàn toàn tranh chấp (contention) ở các bin nóng (ví dụ bin 0 trong dữ liệu lệch 90%), trái ngược với phiên bản `atomic`. |
| **Agglomeration** (Gom nhóm) | Gom nhiều phần tử liền kề thành một khối lớn (chunk) cho mỗi thread thay vì tạo task cho từng phần tử; gom toàn bộ $P$ mảng cục bộ vào một mảng phẳng 1 chiều kích thước $P \times B$. | Giảm chi phí tạo/điều phối tác vụ và tăng tính cục bộ không gian (spatial locality) khi đọc `data[]` và khi gộp `local_hists[]`. |
| **Mapping** (Ánh xạ) | Dùng `#pragma omp for schedule(static)` để ánh xạ các khối gần đều nhau lên $P$ thread; sau đó gộp tuần tự từng bin về mảng kết quả `result.bins`. | Chi phí xử lý mỗi phần tử là hằng số $O(1)$ như nhau, nên `static` cho cân bằng tải tốt nhất với chi phí lập lịch gần như bằng 0. |

---

## 2. Cơ chế đồng bộ OpenMP và đảm bảo tính đúng đắn

1. **Kiểm soát vùng nhớ phẳng $O(P \times B)$ và tránh Data Race**:
   - Thay vì dùng `std::vector<std::vector<uint64_t>>` (gây cấp phát phân mảnh nhiều lần trên heap), chương trình cấp phát **một mảng phẳng liên tiếp duy nhất** `local_hists` có kích thước `actual_threads * stride` (với `stride = B` ở bản chuẩn).
   - Mỗi thread `tid` chỉ đọc/ghi trên đoạn bộ nhớ riêng biệt `[tid * stride, tid * stride + B - 1]`. Vì các đoạn này hoàn toàn rời nhau, **không thể xảy ra data race** trong pha đếm.
2. **Đảm bảo mọi phần đếm hoàn tất trước khi gộp**:
   - Khối `#pragma omp single` cấp phát `local_hists` có **implicit barrier** (hàng rào đồng bộ ngầm) ở cuối khối, đảm bảo mảng cục bộ đã sẵn sàng trước khi bất kỳ thread nào bắt đầu đếm.
   - Cuối vòng lặp `#pragma omp for schedule(static)` và cuối vùng `#pragma omp parallel` đều có **implicit barrier**, đảm bảo 100% các thread đã đếm xong toàn bộ $N$ phần tử trước khi luồng chính thực hiện vòng lặp gộp bin.
3. **Tối ưu truy cập bộ nhớ ở pha gộp**:
   - Vòng lặp gộp đặt `t` (thread) ở ngoài và `b` (bin) ở trong: mỗi lượt duyệt đọc liên tiếp `B` phần tử nằm kề nhau của `thread_hist`, tối ưu hóa cache hit so với duyệt ngược lại.
4. **Ghi nhận số thread thực tế và phạm vi bấm giờ ($T_{\text{total}}$)**:
   - Chương trình ghi lại `actual_threads = omp_get_num_threads()` **bên trong** vùng `#pragma omp parallel` (không chỉ tin vào tham số `requested_threads`).
   - Đồng hồ `omp_get_wtime()` bấm **ngay trước** khi khởi tạo mảng kết quả và mảng cục bộ, và dừng **ngay sau** khi hoàn tất bước gộp bin (đúng theo mục 3.2 của kế hoạch; không tính thời gian sinh dữ liệu đầu vào).

---

## 3. Bảng phân tích bộ nhớ phụ trợ $O(P \times B)$ và chi phí thuật toán

Mỗi bộ đếm bin dùng kiểu `uint64_t` (kích thước **8 byte**) để tránh tràn số khi $N$ lớn. Tổng bộ nhớ phụ trợ của phiên bản cục bộ là:

$$\text{Memory}_{\text{extra}} = P \times \text{stride}(B) \times 8 \text{ bytes}$$

Trong đó:
- Bản chuẩn (`compute_histogram_local`): $\text{stride}(B) = B$.
- Bản mở rộng chống false sharing (`compute_histogram_local_padded`): $\text{stride}(B) = \lceil B / 8 \rceil \times 8$ (làm tròn lên bội số của 1 cache line = 64 byte = 8 phần tử `uint64_t`).

### Bảng 1: Dung lượng bộ nhớ phụ trợ $O(P \times B)$ theo số thread $P$ và số bin $B$

| Số bin $B$ | Kích thước 1 mảng cục bộ ($B \times 8$ B) | $P = 1$ | $P = 2$ | $P = 4$ | $P = 8$ | $P = 16$ | Ghi chú so với L1 Cache (~32–48 KB/lõi) |
| :---: | :---: | :---: | :---: | :---: | :---: | :---: | :--- |
| **16** | 128 B (2 cache lines) | 128 B | 256 B | 512 B | 1 KB | 2 KB | Nằm hoàn toàn trong L1 cache |
| **64** | 512 B (8 cache lines) | 512 B | 1 KB | 2 KB | 4 KB | 8 KB | Nằm hoàn toàn trong L1 cache |
| **256** | 2.048 B (32 cache lines) | 2 KB | 4 KB | 8 KB | 16 KB | 32 KB | Mảng riêng mỗi thread (2 KB) nằm gọn trong L1 |

> **Nhận xét quan trọng**: Với $B \in \{16, 64, 256\}$, cả ba giá trị đều là bội số của 8 (`16 = 2×8`, `64 = 8×8`, `256 = 32×8`), nên mảng cục bộ của từng thread tự nhiên nằm trên các đường cache line 64-byte tách biệt nếu địa chỉ gốc căn chỉnh 64-byte. Tuy nhiên, nếu $B$ không chia hết cho 8 (ví dụ $B = 10$) hoặc địa chỉ cấp phát không căn chỉnh 64-byte, hai thread liền kề có thể chia sẻ chung một đường cache line ở ranh giới cuối mảng (gây **false sharing**). Bản mở rộng `compute_histogram_local_padded` xử lý tổng quát tình huống này.

### Bảng 2: So sánh độ phức tạp và chi phí giữa 3 phương án Histogram

| Tiêu chí | Tuần tự (Serial) | Song song Atomic | Song song Cục bộ & Gộp (Local + Reduction) |
| :--- | :--- | :--- | :--- |
| **Độ phức tạp thời gian** | $O(N + B)$ | $O(N/P + B)$ lý thuyết (tệ hơn nhiều khi tranh chấp cao) | $O(N/P + P \cdot B)$ |
| **Bộ nhớ phụ trợ** | $O(B)$ | $O(B)$ | $O(P \cdot B)$ |
| **Chi phí đồng bộ** | Không có | Rất cao: $N$ thao tác `#pragma omp atomic` trên mảng chung | Thấp: 1 lần fork-join + 2 implicit barrier + vòng gộp $P \cdot B$ |
| **Nhạy cảm dữ liệu lệch (Skewed)** | Không | **Rất nặng**: 90% thread cùng tranh chấp ghi vào `H[0]` | **Không nhạy cảm**: mỗi thread ghi vào `my_hist[0]` riêng |
| **Nhạy cảm số bin $B$ nhỏ** | Không | **Tăng tranh chấp** vì mật độ va chạm trên mỗi bin tăng lên | **Hầu như không đổi** (thậm chí gộp nhanh hơn vì $P \cdot B$ nhỏ) |

---

## 4. Phân tích hiệu năng & Giải thích các trường hợp Speedup thấp hoặc dưới 1 ($S_p < 1$)

Khi đánh giá các biểu đồ từ thí nghiệm Nhóm A (theo $N, p$, phân phối) và Nhóm B (theo số bin $B$), cần lưu ý các nguyên nhân bản chất sau đây để giải thích đúng số liệu thực nghiệm (tuyệt đối giữ số liệu thật, không chỉnh sửa):

1. **Trường hợp $S_p < 1$ ở kích thước $N$ nhỏ (Overhead lấn át công việc tính toán)**:
   - Mở vùng `#pragma omp parallel` tốn chi phí đánh thức/đồng bộ các thread (fork-join overhead), cấp phát và xóa về 0 vùng nhớ `local_hists` kích thước $P \times B$, cùng bước gộp tuần tự $O(P \cdot B)$.
   - Khi $N$ nhỏ (ví dụ $N \le 10^6$), thời gian quét tuần tự chỉ tính bằng vài trăm micro-giây đến dưới 1 mili-giây, nên tổng chi phí phụ trợ trên có thể làm $T_{\text{parallel}} > T_{\text{serial}} \Rightarrow S_p < 1$.
2. **Speedup bão hòa dưới lý thuyết ($S_p < P$, $E_p$ giảm khi tăng $p$) do giới hạn băng thông bộ nhớ (Memory-bound)**:
   - Mỗi phần tử `uint8_t` chỉ thực hiện 2–3 phép toán đơn giản (đọc 1 byte, nhân/dịch bit, cộng 1 vào bin). Tỷ lệ tính toán trên mỗi byte đọc từ bộ nhớ (Arithmetic Intensity) rất thấp.
   - Với $N = 10^8$ (100 MB dữ liệu đầu vào, vượt xa dung lượng L3 cache), các lõi CPU phải đọc liên tục từ RAM. Khi số thread tăng lên, băng thông bộ nhớ chính (Memory Bandwidth) nhanh chóng bị bão hòa, khiến việc thêm thread không làm tăng tốc độ tuyến tính nữa.
3. **Suy giảm hiệu quả khi dùng nhiều thread hơn số lõi vật lý (SMT / Hyper-Threading)**:
   - Nếu $P$ chọn bằng số logical CPU (gấp đôi số lõi vật lý), các cặp luồng logic trên cùng một lõi vật lý phải chia sẻ chung đơn vị thực thi, bộ nhớ đệm L1/L2 và băng thông tải dữ liệu, dẫn đến hiệu suất $E_p = S_p / P$ giảm rõ rệt.
4. **Sự đối lập giữa Atomic và Local trên dữ liệu lệch (Skewed 90% = 0)**:
   - Trên bản **Atomic**, 90% thao tác ghi từ mọi lõi đều nhắm vào cùng một địa chỉ `H[0]`. Theo giao thức đồng nhất cache (MESI/MOESI), đường cache chứa `H[0]` phải liên tục bị vô hiệu hóa và di chuyển qua lại giữa các lõi (**cache-line bouncing**), khiến bản Atomic có thể chậm hơn cả bản tuần tự ($S_p \ll 1$).
   - Trên bản **Local**, mỗi thread ghi vào `my_hist[0]` riêng trong L1 cache của chính lõi đó nên gần như không chịu phạt tranh chấp; thời gian chạy trên dữ liệu skewed tương đương (thậm chí nhỉnh hơn một chút nhờ `my_hist[0]` luôn nóng trong L1 cache) so với dữ liệu uniform.
5. **Ảnh hưởng của số bin $B \in \{16, 64, 256\}$ (Thí nghiệm Nhóm B)**:
   - Với bản **Atomic**, giảm $B$ từ 256 xuống 16 làm xác suất hai thread cùng ghi vào một bin (hoặc cùng một cache line chứa 8 bin) tăng mạnh, nên tốc độ giảm rõ rệt ngay cả với dữ liệu uniform.
   - Với bản **Local**, vì $256 \times 8 = 2\text{ KB}$ vẫn rất nhỏ so với L1 cache, thời gian chạy giữa $B = 16, 64, 256$ khác biệt rất ít; mức chênh nhỏ (nếu có) đến từ bước khởi tạo và gộp $O(P \cdot B)$.

---

## 5. Giới hạn của thí nghiệm (Limitations)

- **Phạm vi phần cứng**: Thực nghiệm chạy trên một máy tính cá nhân dùng CPU đa lõi (shared memory), chưa đánh giá trên hệ thống đa socket NUMA hoặc GPU.
- **Nhiễu nền hệ điều hành**: Dù đã chạy warm-up, lặp ít nhất 5 lần và dùng **median + IQR / min–max** để giảm ảnh hưởng nhiễu, các tiến trình nền của hệ điều hành và cơ chế tăng/giảm xung nhịp tự động (CPU Turbo Boost / thermal throttling) vẫn có thể gây dao động nhẹ giữa các lượt đo.
- **Kích thước dữ liệu lớn bị ràng buộc bởi RAM**: Mức $N$ lớn nhất ($10^8$ phần tử = 100 MB) được chọn để vừa đủ vượt L3 cache nhưng không gây tràn RAM hay swap ổ đĩa.
- **Phạm vi thuật toán**: Bước gộp được cài đặt tuần tự $O(P \cdot B)$ vì $P \cdot B$ rất nhỏ; nếu mở rộng sang hàng trăm nghìn bin ($B \ge 10^5$) hoặc hàng trăm lõi, sẽ cần gộp song song theo cây (tree reduction) hoặc chia bin cho các thread gộp song song.

---

## 6. Khung bảng tổng hợp kết quả thực nghiệm (Điền từ `results/summary.csv` thật)

> *Lưu ý tuân thủ mục B.2: Không tự điền số liệu giả. Sau khi chạy `scripts/run_bench.py` (Thành viên 3) cho Nhóm A (TV1) và Nhóm B (TV2) trên cùng máy đo cố định, nhóm điền trực tiếp giá trị thật từ `results/summary.csv` vào hai bảng dưới đây.*

### Bảng 3: Kết quả Thí nghiệm Nhóm B — Ảnh hưởng của số bin $B$ (giữ $N$ lớn cố định, $p = P$)

| Phân phối | Số bin $B$ | Median $T_{\text{serial}}$ (ms) | Median $T_{\text{atomic}}$ (ms) | $S_p$ Atomic | Median $T_{\text{local}}$ (ms) | $S_p$ Local | $E_p$ Local | Độ phân tán (IQR / min–max) |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| Uniform | 16 | *(điền từ CSV)* | *(điền từ CSV)* | | | | | |
| Uniform | 64 | *(điền từ CSV)* | *(điền từ CSV)* | | | | | |
| Uniform | 256 | *(điền từ CSV)* | *(điền từ CSV)* | | | | | |
| Skewed (90%=0) | 16 | *(điền từ CSV)* | *(điền từ CSV)* | | | | | |
| Skewed (90%=0) | 64 | *(điền từ CSV)* | *(điền từ CSV)* | | | | | |
| Skewed (90%=0) | 256 | *(điền từ CSV)* | *(điền từ CSV)* | | | | | |
