cmake_minimum_required(VERSION 3.16)
set(PROJ_NAME gpio-irq-test)
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
#        LL_RCC
#        LL_PWR
#        LL_UTILS
#        LL_GPIO
#        LL_EXTI
        REQUIRED)

add_definitions(-DUSE_FULL_LL_DRIVER)

# Clock speed definitions for custom hardware
add_definitions(-DHSE_VALUE=8000000)
add_definitions(-DLSE_VALUE=32768)

#add_definitions(-DUART_POLL)
add_definitions(-DUART_IRQ)
#add_definitions(-DUART_DMA)

#add_definitions(-DPAYLOAD_12B)
#add_definitions(-DPAYLOAD_128B)
add_definitions(-DPAYLOAD_1024B)


add_executable(${PROJ_NAME})

target_sources(
        ${PROJ_NAME}
        PRIVATE
        ${CMAKE_SOURCE_DIR}/src/main.c
        ${CMAKE_SOURCE_DIR}/src/uart.c
        ${CMAKE_SOURCE_DIR}/src/fifo.c
)

target_include_directories(
        ${PROJ_NAME}
        PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(
        ${PROJ_NAME}
        PRIVATE
        HAL::STM32::F4::LL_RCC
        HAL::STM32::F4::LL_PWR
        HAL::STM32::F4::LL_UTILS
        HAL::STM32::F4::LL_GPIO
        HAL::STM32::F4::LL_EXTI
        HAL::STM32::F4::LL_USART
        HAL::STM32::F4::LL_DMA
        CMSIS::STM32::F429xx
        STM32::NoSys
)

target_link_options(${PROJ_NAME} PRIVATE -Wl,--print-memory-usage)
stm32_add_linker_script(${PROJ_NAME} PRIVATE F429ZITx.ld)

stm32_generate_binary_file(${PROJ_NAME})
stm32_generate_hex_file(${PROJ_NAME})