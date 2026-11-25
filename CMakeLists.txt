# =============================================================================
# Tệp: CMakeLists.txt
# Tác giả: Thành viên 2 - Histogram cục bộ và phân tích hiệu năng
# Mô tả: Cấu hình biên dịch dự án C++ + OpenMP với cờ tối ưu cố định (-O3 / /O2),
#        hỗ trợ chạy độc lập phần của Thành viên 2 và tự động tích hợp các tệp
#        của Thành viên 1 & Thành viên 3 khi ghép nhánh Git.
# =============================================================================

# Yêu cầu phiên bản CMake tối thiểu 3.16 để hỗ trợ tốt OpenMP::OpenMP_CXX
cmake_minimum_required(VERSION 3.16)

# Khai báo tên dự án, phiên bản và ngôn ngữ lập trình chính là C++
project(ParallelHistogramProject VERSION 1.0 LANGUAGES CXX)

# -----------------------------------------------------------------------------
# 1. Thiết lập chuẩn C++ và chế độ build mặc định (Release để đo hiệu năng)
# -----------------------------------------------------------------------------
# Sử dụng chuẩn C++17 (hỗ trợ tốt constexpr, std::vector, inline, v.v.)
set(CMAKE_CXX_STANDARD 17)
# Bắt buộc trình biên dịch phải hỗ trợ đúng C++17, không tự hạ xuống chuẩn cũ
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Tắt các phần mở rộng riêng của trình biên dịch để mã nguồn chuẩn mực đa nền tảng
set(CMAKE_CXX_EXTENSIONS OFF)

# Nếu người dùng không chỉ định CMAKE_BUILD_TYPE (khi dùng Makefile/Ninja),
# mặc định chọn Release để bật cờ tối ưu tốc độ cho các thí nghiệm benchmark
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Che do bien dich (Debug/Release)" FORCE)
endif()

# -----------------------------------------------------------------------------
# 2. Tìm thư viện OpenMP (bắt buộc cho phiên bản song song CPU)
# -----------------------------------------------------------------------------
# Tìm gói OpenMP cho C++; dừng báo lỗi ngay nếu trình biên dịch không hỗ trợ OpenMP
find_package(OpenMP REQUIRED)

# -----------------------------------------------------------------------------
# 3. Hàm nội bộ gắn cờ biên dịch chuẩn (cờ tối ưu + cảnh báo) cho từng target
# -----------------------------------------------------------------------------
function(apply_project_flags target_name)
    # Gắn thư mục include/ để mọi tệp nguồn có thể #include "histogram.hpp"
    target_include_directories(${target_name} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
    # Liên kết với thư viện OpenMP (tự động thêm cờ -fopenmp hoặc /openmp)
    target_link_libraries(${target_name} PUBLIC OpenMP::OpenMP_CXX)

    # Thiết lập cờ tối ưu và cảnh báo tùy theo trình biên dịch (MSVC hoặc GCC/Clang)
    if(MSVC)
        # /O2: tối ưu tốc độ chuẩn trên MSVC; /W4: bật mức cảnh báo cao
        target_compile_options(${target_name} PRIVATE /W4 $<$<CONFIG:Release>:/O2>)
    else()
        # -O3: tối ưu tốc độ tối đa trên GCC/Clang (nhất quán cho toàn bộ benchmark)
        # -Wall -Wextra: bật cảnh báo để sớm phát hiện lỗi biến thừa, tràn dấu, v.v.
        target_compile_options(${target_name} PRIVATE -Wall -Wextra $<$<CONFIG:Release>:-O3>)
    endif()
endfunction()

# -----------------------------------------------------------------------------
# 4. Phần lõi của Thành viên 2: Thư viện Histogram Cục bộ & Bộ kiểm thử đối chiếu
# -----------------------------------------------------------------------------
# Tạo thư viện tĩnh chứa cài đặt histogram cục bộ của Thành viên 2
add_library(histogram_local_lib STATIC
    src/histogram_local.cpp
)
# Gắn cờ OpenMP và cờ tối ưu cho thư viện histogram_local_lib
apply_project_flags(histogram_local_lib)

# Tạo tệp thực thi kiểm thử đối chiếu cho Thành viên 2
add_executable(test_histogram_local
    tests/test_histogram_local.cpp
)
# Liên kết bộ test với thư viện histogram_local_lib
target_link_libraries(test_histogram_local PRIVATE histogram_local_lib)
# Gắn cờ OpenMP và cờ tối ưu cho tệp thực thi kiểm thử
apply_project_flags(test_histogram_local)

# -----------------------------------------------------------------------------
# 5. Tự động tích hợp mã nguồn của Thành viên 1 & Thành viên 3 khi ghép nhánh
# -----------------------------------------------------------------------------
# Kiểm tra nếu Thành viên 1 đã bàn giao đầy đủ các tệp nguồn histogram nền tảng
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/histogram_serial.cpp" AND
   EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/histogram_atomic.cpp")
    # Gom cả 3 phiên bản (tuần tự, atomic, cục bộ) thành một thư viện histogram thống nhất
    set(HIST_ALL_SRCS
        src/histogram_serial.cpp
        src/histogram_atomic.cpp
        src/histogram_local.cpp
    )
    # Nếu có tệp sinh dữ liệu dùng chung thì bổ sung vào thư viện
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/generate_data.cpp")
        list(APPEND HIST_ALL_SRCS src/generate_data.cpp)
    endif()

    # Tạo thư viện tổng hợp cho toàn bộ bài Histogram
    add_library(histogram_all_lib STATIC ${HIST_ALL_SRCS})
    apply_project_flags(histogram_all_lib)

    # Nếu Thành viên 1 có tệp test tổng hợp thì biên dịch và đăng ký vào CTest
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_histogram.cpp")
        add_executable(test_histogram tests/test_histogram.cpp)
        target_link_libraries(test_histogram PRIVATE histogram_all_lib)
        apply_project_flags(test_histogram)
    endif()
endif()

# Kiểm tra nếu Thành viên 3 đã bàn giao bài chung Tích vô hướng (Dot Product)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/dot_product.cpp" AND
   EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_dot.cpp")
    # Tạo tệp thực thi kiểm thử bài tích vô hướng
    add_executable(test_dot
        src/dot_product.cpp
        tests/test_dot.cpp
    )
    apply_project_flags(test_dot)
endif()

# -----------------------------------------------------------------------------
# 6. Kích hoạt CTest để chạy toàn bộ bộ kiểm thử bằng một lệnh (ctest)
# -----------------------------------------------------------------------------
enable_testing()

# Đăng ký bộ kiểm thử của Thành viên 2 vào danh sách CTest
add_test(NAME TestHistogramLocal COMMAND test_histogram_local)

# Đăng ký tự động bộ kiểm thử của Thành viên 1 (nếu đã ghép nhánh)
if(TARGET test_histogram)
    add_test(NAME TestHistogramAll COMMAND test_histogram)
endif()

# Đăng ký tự động bộ kiểm thử của Thành viên 3 (nếu đã ghép nhánh)
if(TARGET test_dot)
    add_test(NAME TestDotProduct COMMAND test_dot)
endif()
