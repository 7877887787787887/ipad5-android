cmake_minimum_required(VERSION 3.16)
project(j72s_host_tests LANGUAGES C CXX)
enable_testing()
find_package(Python3 REQUIRED COMPONENTS Interpreter)
find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(LibDrm REQUIRED IMPORTED_TARGET libdrm)
get_filename_component(repo "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)

foreach(unit IN ITEMS lights/backlight power/battery)
    get_filename_component(name "${unit}" NAME)
    add_executable("${name}_test" "${unit}_test.cpp")
    target_compile_features("${name}_test" PRIVATE cxx_std_17)
    target_compile_options("${name}_test" PRIVATE -Wall -Wextra -Werror -UNDEBUG)
    add_test(NAME "${name}" COMMAND "${name}_test")
endforeach()

foreach(unit IN ITEMS android-stage/probe hardware-stage/hw_report hardware-stage/menu)
    get_filename_component(name "${unit}" NAME)
    add_executable("${name}" "${repo}/${unit}.c")
    target_compile_features("${name}" PRIVATE c_std_11)
    target_compile_options("${name}" PRIVATE -Wall -Wextra -Werror -UNDEBUG)
    add_test(NAME "${name}" COMMAND "${name}" --self-test)
endforeach()

add_executable(device_interfaces "${repo}/validation/device_interfaces.c")
target_compile_features(device_interfaces PRIVATE c_std_11)
target_compile_options(device_interfaces PRIVATE -Wall -Wextra -Werror)
target_link_libraries(device_interfaces PRIVATE PkgConfig::LibDrm)
add_test(NAME device_interfaces COMMAND device_interfaces --self-test)

add_executable(vxd_probe_test
    "${repo}/android-stage/video/vxd_probe.c" "${repo}/android-stage/video/vxd_probe_test.c")
target_compile_features(vxd_probe_test PRIVATE c_std_17)
target_compile_definitions(vxd_probe_test PRIVATE J72S_VXD_PROBE_TEST)
target_compile_options(vxd_probe_test PRIVATE -Wall -Wextra -Werror -UNDEBUG)
target_link_options(vxd_probe_test PRIVATE -Wl,--wrap=ioctl,--wrap=close)
add_test(NAME vxd_probe COMMAND vxd_probe_test)

add_executable(alsa_endpoint_test "${repo}/android-stage/alsa_endpoint.c"
    "${repo}/android-stage/alsa_endpoint_test.c")
target_compile_features(alsa_endpoint_test PRIVATE c_std_17)
target_compile_options(alsa_endpoint_test PRIVATE -Wall -Wextra -Werror -UNDEBUG)
target_link_options(alsa_endpoint_test PRIVATE -Wl,--wrap=open,--wrap=close,--wrap=ioctl)
add_test(NAME alsa_endpoint COMMAND alsa_endpoint_test)

add_executable(alsa_controls_test "${repo}/android-stage/audio-primary/alsa_controls.c"
    "${repo}/android-stage/audio-primary/alsa_controls_test.c")
target_compile_features(alsa_controls_test PRIVATE c_std_17)
target_compile_options(alsa_controls_test PRIVATE -Wall -Wextra -Werror -UNDEBUG)
target_link_options(alsa_controls_test PRIVATE -Wl,--wrap=open,--wrap=close,--wrap=ioctl)
add_test(NAME alsa_controls COMMAND alsa_controls_test)

add_executable(iio_sensors_test
    "${repo}/android-stage/sensors/iio_sensors.cpp"
    "${repo}/android-stage/sensors/iio_sensors_test.cpp")
target_compile_features(iio_sensors_test PRIVATE cxx_std_17)
target_compile_options(iio_sensors_test PRIVATE -Wall -Wextra -Werror -UNDEBUG)
add_test(NAME iio_sensors COMMAND iio_sensors_test)

add_executable(bluetooth_hci_test
    "${repo}/android-stage/bluetooth/j72s_hci_transport.cpp"
    "${repo}/android-stage/bluetooth/j72s_hci_transport_test.cpp")
target_compile_features(bluetooth_hci_test PRIVATE cxx_std_17)
target_compile_options(bluetooth_hci_test PRIVATE -Wall -Wextra -Werror -UNDEBUG)
target_link_libraries(bluetooth_hci_test PRIVATE Threads::Threads)
add_test(NAME bluetooth_hci COMMAND bluetooth_hci_test)

add_executable(wifi_interface_test
    "${repo}/android-stage/wifi/wifi_interface.cpp"
    "${repo}/android-stage/wifi/wifi_interface_test.cpp")
target_compile_features(wifi_interface_test PRIVATE cxx_std_17)
target_compile_options(wifi_interface_test PRIVATE -Wall -Wextra -Werror -UNDEBUG)
add_test(NAME wifi_interface COMMAND wifi_interface_test)

find_package(Vulkan QUIET)
if(Vulkan_FOUND)
    add_executable(vulkan_probe_test "${repo}/android-stage/vulkan_probe.c")
    target_compile_features(vulkan_probe_test PRIVATE c_std_17)
    target_compile_options(vulkan_probe_test PRIVATE -Wall -Wextra -Werror)
    target_link_libraries(vulkan_probe_test PRIVATE Vulkan::Vulkan)
    add_test(NAME vulkan_submit COMMAND vulkan_probe_test --allow-software)
    set_tests_properties(vulkan_submit PROPERTIES TIMEOUT 20 SKIP_RETURN_CODE 77)
endif()

add_test(NAME kernel_config_audit
    COMMAND Python3::Interpreter -B -m unittest discover -s "${repo}/scripts" -p test_audit.py -v)
add_test(NAME vendor_modules
    COMMAND Python3::Interpreter -B -m unittest discover -s "${repo}/scripts" -p test_vendor_modules.py -v)
add_test(NAME vendor_product
    COMMAND Python3::Interpreter -B -m unittest discover -s "${repo}/scripts" -p test_vendor_product.py -v)
add_test(NAME vendor_firmware
    COMMAND Python3::Interpreter -B -m unittest discover -s "${repo}/scripts" -p test_stage_j72s_firmware.py -v)
add_test(NAME early_usb_storage
    COMMAND Python3::Interpreter -B -m unittest discover -s "${repo}/scripts" -p test_stage_j72s_early_storage.py -v)
add_test(NAME boot_media
    COMMAND Python3::Interpreter -B -m unittest discover -s "${repo}/validation" -p test_check_j72s_boot_media.py -v)
add_test(NAME first_stage_package
    COMMAND Python3::Interpreter -B -m unittest discover -s "${repo}/validation" -p test_package_j72s_first_stage.py -v)
add_test(NAME clock_audit
    COMMAND Python3::Interpreter -B "${repo}/performance/audit_clock.py" --self-test)
add_test(NAME ota_verifier
    COMMAND Python3::Interpreter -B "${repo}/hardware-stage/ota_verify.py")
