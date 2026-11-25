# =============================================================================
# Tệp: scripts/build.ps1
# Tác giả: Thành viên 2 - Histogram cục bộ và phân tích hiệu năng
# Mô tả: Script build và chạy kiểm thử tự động trên Windows PowerShell.
#        Ưu tiên dùng CMake nếu có generator phù hợp; tự động dự phòng biên dịch
#        trực tiếp bằng g++ -O3 -fopenmp để chạy được ngay trên mọi máy có MinGW/MSYS2.
# =============================================================================

# Dừng script ngay lập tức nếu bất kỳ lệnh nào trả về lỗi
$ErrorActionPreference = "Stop"

# Xác định đường dẫn thư mục gốc của dự án (thư mục cha của thư mục scripts/)
$ProjectRoot = Split-Path -Parent $PSScriptRoot
# Xác định thư mục chứa sản phẩm biên dịch
$BuildDir = Join-Path $ProjectRoot "build"

# Tạo thư mục build/ nếu chưa tồn tại
if (-not (Test-Path $BuildDir)) {
    New-Item -ItemType Directory -Path $BuildDir | Out-Null
}

Write-Host "=== [1/3] Kiem tra cong cu bien dich ===" -ForegroundColor Cyan

# Kiểm tra sự tồn tại của cmake và các bộ sinh build phổ biến (ninja / mingw32-make)
$HasCmake = Get-Command cmake -ErrorAction SilentlyContinue
$HasNinja = Get-Command ninja -ErrorAction SilentlyContinue
$HasMingwMake = Get-Command mingw32-make -ErrorAction SilentlyContinue
$HasGpp = Get-Command g++ -ErrorAction SilentlyContinue

# Nếu có CMake kèm Ninja hoặc MinGW Makefiles thì build bằng CMake chuẩn
if ($HasCmake -and ($HasNinja -or $HasMingwMake)) {
    # Chọn generator ưu tiên Ninja (nhanh hơn), nếu không thì dùng MinGW Makefiles
    $Generator = if ($HasNinja) { "Ninja" } else { "MinGW Makefiles" }
    Write-Host "Su dung CMake voi generator: $Generator (Release, -O3, OpenMP)" -ForegroundColor Green

    # Cấu hình dự án CMake ở chế độ Release
    cmake -S $ProjectRoot -B $BuildDir -G $Generator -DCMAKE_BUILD_TYPE=Release

    Write-Host "=== [2/3] Dang bien dich du an ===" -ForegroundColor Cyan
    # Thực hiện biên dịch toàn bộ các target đã khai báo trong CMakeLists.txt
    cmake --build $BuildDir --config Release

    Write-Host "=== [3/3] Dang chay kiem thu bang CTest ===" -ForegroundColor Cyan
    # Chạy toàn bộ bộ test đã đăng ký và in chi tiết nếu có lỗi
    ctest --test-dir $BuildDir --output-on-failure -V
}
elseif ($HasGpp) {
    # Nhánh dự phòng: biên dịch trực tiếp bằng g++ với đúng chuẩn C++17, -O3 và -fopenmp
    Write-Host "Khong tim thay Ninja/MinGW-Make cho CMake; bien dich truc tiep bang g++ (-O3 -fopenmp)" -ForegroundColor Yellow

    # Đường dẫn tệp thực thi đầu ra
    $OutExe = Join-Path $BuildDir "test_histogram_local.exe"

    Write-Host "=== [2/3] Dang bien dich test_histogram_local.exe ===" -ForegroundColor Cyan
    # Gọi g++ biên dịch mã nguồn histogram_local.cpp và test_histogram_local.cpp
    & g++ -std=c++17 -O3 -Wall -Wextra -fopenmp `
        -I (Join-Path $ProjectRoot "include") `
        (Join-Path $ProjectRoot "src\histogram_local.cpp") `
        (Join-Path $ProjectRoot "tests\test_histogram_local.cpp") `
        -o $OutExe

    Write-Host "=== [3/3] Dang chay test_histogram_local.exe ===" -ForegroundColor Cyan
    # Thực thi tệp kiểm thử vừa biên dịch
    & $OutExe
}
else {
    # Thông báo lỗi rõ ràng nếu máy chưa cài cả CMake lẫn g++
    Write-Error "Khong tim thay trinh bien dich C++ (g++ hoac CMake). Vui long cai dat MSYS2/MinGW-w64 hoac Visual Studio."
}
