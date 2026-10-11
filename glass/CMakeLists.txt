cmake_minimum_required(VERSION 3.22)
project(glassy_blacknpink_glass LANGUAGES CXX)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)
find_package(ECM REQUIRED NO_MODULE)
list(APPEND CMAKE_MODULE_PATH ${ECM_MODULE_PATH})
find_package(Qt6 REQUIRED COMPONENTS Quick Qml Widgets)
include(KDEInstallDirs6)
find_package(KF6WindowSystem REQUIRED)
find_package(KWin REQUIRED COMPONENTS kwineffects)
find_package(KDecoration3 REQUIRED)

# Lets Plasma widgets such as Glassy Black & Pink Island request rounded glass regions.
add_library(glassyblacknpinkglass MODULE blurregion.cpp)
target_link_libraries(glassyblacknpinkglass PRIVATE Qt6::Quick Qt6::Qml KF6::WindowSystem)
install(TARGETS glassyblacknpinkglass DESTINATION ${KDE_INSTALL_QMLDIR}/GlassyBlacknpink/Glass)
configure_file(qmldir "${CMAKE_CURRENT_BINARY_DIR}/qmldir" COPYONLY)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/qmldir" DESTINATION ${KDE_INSTALL_QMLDIR}/GlassyBlacknpink/Glass)

# The KWin effect. Each build gets its own plugin name, so KWin loads the new
# build instead of a copy it already holds in memory. Its shaders sit under
# that name too: KWin keeps earlier builds loaded, and Qt reads a resource
# path from the first library that registered it.
add_library(glass MODULE glass.cpp)
set_target_properties(glass PROPERTIES OUTPUT_NAME "${GLASS_EFFECT_ID}" PREFIX "")
target_compile_definitions(glass PRIVATE GLASS_EFFECT_ID="${GLASS_EFFECT_ID}")
qt_add_resources(glass glass PREFIX "/${GLASS_EFFECT_ID}" FILES downsample.frag upsample.frag detail.frag busy.frag glass.frag)
target_link_libraries(glass PRIVATE KWin::kwin KDecoration3::KDecoration)
install(TARGETS glass DESTINATION ${KDE_INSTALL_PLUGINDIR}/kwin/effects/plugins)
