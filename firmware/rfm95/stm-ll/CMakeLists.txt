cmake_minimum_required(VERSION 3.16)
set(PROJ_NAME rfm95-test)
set(STM32_CMAKE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/stm32-cmake)
set(CMAKE_TOOLCHAIN_FILE ${STM32_CMAKE_PATH}/cmake/stm32_gcc.cmake)

#SET(CMAKE_CXX_FLAGS "-O1")
#SET(CMAKE_C_FLAGS "-O1")

project(${PROJ_NAME} C ASM)
set(CMAKE_INCLUDE_CURRENT_DIR TRUE)

stm32_fetch_cmsis(F4)
stm32_fetch_hal(F4)

find_package(CMSIS COMPONENTS STM32F4 REQUIRED)
find_package(HAL
        COMPONENTS
        STM32F4
        REQUIRED)

add_definitions(-DUSE_FULL_LL_DRIVER)

# Clock speed definitions for custom hardware
add_definitions(-DHSE_VALUE=8000000)
add_definitions(-DLSE_VALUE=32768)

add_executable(${PROJ_NAME})

target_sources(
        ${PROJ_NAME}
        PRIVATE
        ${CMAKE_SOURCE_DIR}/src/main.c
        ${CMAKE_SOURCE_DIR}/libs/rfm95.c
)

target_include_directories(
        ${PROJ_NAME}
        PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/libs/

)

target_link_libraries(
        ${PROJ_NAME}
        PRIVATE
        HAL::STM32::F4::LL_RCC
        HAL::STM32::F4::LL_PWR
        HAL::STM32::F4::LL_UTILS
        HAL::STM32::F4::LL_GPIO
        HAL::STM32::F4::LL_EXTI
        HAL::STM32::F4::LL_SPI
        HAL::STM32::F4::LL_DMA
        CMSIS::STM32::F429xx
        STM32::NoSys
)

target_link_options(${PROJ_NAME} PRIVATE -Wl,--print-memory-usage)
stm32_add_linker_script(${PROJ_NAME} PRIVATE F429ZITx.ld)

stm32_generate_binary_file(${PROJ_NAME})
stm32_generate_hex_file(${PROJ_NAME})