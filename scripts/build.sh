#!/usr/bin/env bash
# =============================================================================
# Tệp: scripts/build.sh
# Tác giả: Thành viên 2 - Histogram cục bộ và phân tích hiệu năng
# Mô tả: Script build và chạy kiểm thử tự động trên Linux / macOS / WSL / MSYS2.
#        Sử dụng CMake (chế độ Release, -O3, OpenMP) và chạy CTest.
# =============================================================================

# Thoát ngay khi có bất kỳ lệnh nào thất bại (-e) hoặc dùng biến chưa khai báo (-u)
set -euo pipefail

# Xác định đường dẫn tuyệt đối tới thư mục gốc của dự án
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"

echo "=== [1/3] Cau hinh CMake (Release, -O3, OpenMP) ==="
# Cấu hình CMake ở chế độ Release
cmake -S "${PROJECT_ROOT}" -B "${BUILD_DIR}" -DCMAKE_BUILD_TYPE=Release

echo "=== [2/3] Dang bien dich du an ==="
# Biên dịch song song các target trong CMakeLists.txt
cmake --build "${BUILD_DIR}" --config Release

echo "=== [3/3] Dang chay bo kiem thu bang CTest ==="
# Chạy toàn bộ test đã đăng ký và hiển thị log chi tiết
ctest --test-dir "${BUILD_DIR}" --output-on-failure -V
