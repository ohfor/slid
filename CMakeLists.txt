cmake_minimum_required(VERSION 3.21)

# ---- Versioning ----
set(VERSION_MAJOR 1)
set(VERSION_MINOR 0)
set(VERSION_PATCH 0)

# ---- Project ----
project(
    SLID
    VERSION ${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH}
    LANGUAGES CXX
)

# ---- Include guards ----
if(PROJECT_SOURCE_DIR STREQUAL PROJECT_BINARY_DIR)
    message(FATAL_ERROR "In-source builds are not allowed.")
endif()

# ---- C++ Standard ----
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_DEBUG OFF)

# ---- Strip source paths from binaries (Release only) ----
# Removes full filesystem paths from __FILE__ macros in all targets including dependencies.
# Without this, your local directory structure leaks into the shipped DLL.
if(MSVC)
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /d1trimfile:\"${CMAKE_SOURCE_DIR}/\"")
    set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} /d1trimfile:\"${CMAKE_SOURCE_DIR}/\"")
    set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} /DEBUG:NONE")
    set(CMAKE_SHARED_LINKER_FLAGS_RELEASE "${CMAKE_SHARED_LINKER_FLAGS_RELEASE} /DEBUG:NONE")
endif()

# ---- Dependencies ----
# spdlog and xbyak come from vcpkg
find_package(spdlog CONFIG REQUIRED)

find_package(minhook CONFIG REQUIRED)

# CommonLibSSE-NG via FetchContent (SE/AE/VR in single DLL)
include(FetchContent)
message(STATUS "Fetching CommonLibSSE-NG...")
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ENABLE_SKYRIM_SE ON CACHE BOOL "" FORCE)
set(ENABLE_SKYRIM_AE ON CACHE BOOL "" FORCE)
set(ENABLE_SKYRIM_VR ON CACHE BOOL "" FORCE)
set(ENABLE_XBYAK ON CACHE BOOL "" FORCE)
FetchContent_Declare(
    CommonLibSSE
    GIT_REPOSITORY https://github.com/CharmedBaryon/CommonLibSSE-NG.git
    GIT_TAG v3.7.0
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(CommonLibSSE)

# ---- Source files ----
set(SOURCES
    src/main.cpp
    src/NetworkManager.cpp
    src/Distributor.cpp
    src/ConsoleCommands.cpp
    src/ActivationHook.cpp
    src/UIHelper.cpp
    src/Feedback.cpp
    src/SLIDMenu.cpp
    src/ConfirmDialog.cpp
    src/TagInputMenu.cpp
    src/WhooshConfigMenu.cpp
    src/ChecklistGrid.cpp
    src/ScaleformUtil.cpp
    src/Settings.cpp
    src/SalesProcessor.cpp
    src/SellOverviewMenu.cpp
    src/SummonChest.cpp
    src/FilterRegistry.cpp
    src/TraitEvaluator.cpp
    src/ConfigState.cpp
    src/ActionBar.cpp
    src/ContainerScanner.cpp
    src/OriginPanel.cpp
    src/HoldRemove.cpp
    src/FilterPanel.cpp
    src/FilterRow.cpp
    src/CatchAllPanel.cpp
    src/Dropdown.cpp
    src/VendorRegistry.cpp
    src/SCIEIntegration.cpp
    src/TranslationService.cpp
    src/APIMessaging.cpp
    src/WelcomeMenu.cpp
)

set(HEADERS
    include/PCH.h
    include/Version.h
    include/Network.h
    include/NetworkManager.h
    include/UIHelper.h
    include/Settings.h
    src/Distributor.h
    src/ConsoleCommands.h
    src/ActivationHook.h
    src/Feedback.h
    src/SLIDMenu.h
    src/ConfirmDialog.h
    src/TagInputMenu.h
    src/WhooshConfigMenu.h
    include/ChecklistGrid.h
    include/DirectionalInput.h
    include/ScaleformUtil.h
    src/SalesProcessor.h
    src/SellOverviewMenu.h
    src/SummonChest.h
    include/IFilter.h
    include/FilterRegistry.h
    include/TraitEvaluator.h
    include/ConfigState.h
    src/ActionBar.h
    include/ContainerScanner.h
    src/OriginPanel.h
    src/HoldRemove.h
    src/FilterPanel.h
    src/FilterRow.h
    src/CatchAllPanel.h
    include/Dropdown.h
    include/VendorRegistry.h
    include/SCIEIntegration.h
    include/TranslationService.h
    include/APIMessaging.h
    src/WelcomeMenu.h
)

# ---- Create DLL ----
add_library(
    ${PROJECT_NAME}
    SHARED
    ${SOURCES}
    ${HEADERS}
    ${CMAKE_CURRENT_SOURCE_DIR}/version.rc
)

target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_23)

target_include_directories(
    ${PROJECT_NAME}
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(
    ${PROJECT_NAME}
    PRIVATE
    CommonLibSSE::CommonLibSSE
    minhook::minhook
)

target_precompile_headers(
    ${PROJECT_NAME}
    PRIVATE
    include/PCH.h
)

# ---- Compiler options ----
if(MSVC)
    target_compile_options(
        ${PROJECT_NAME}
        PRIVATE
        /W4
        /WX-
        /permissive-
        /Zc:__cplusplus
        /Zc:preprocessor
        /external:anglebrackets
        /external:W0
        /MP
    )
endif()

# ---- Post build ----
# Set SKYRIM_MODS_PATH env var to auto-copy DLL to your mod manager's staging folder
if(DEFINED ENV{SKYRIM_MODS_PATH})
    set(SKYRIM_MODS_PATH $ENV{SKYRIM_MODS_PATH})
endif()

if(DEFINED SKYRIM_MODS_PATH)
    add_custom_command(
        TARGET ${PROJECT_NAME}
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${SKYRIM_MODS_PATH}/${PROJECT_NAME}/SKSE/Plugins"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:${PROJECT_NAME}> "${SKYRIM_MODS_PATH}/${PROJECT_NAME}/SKSE/Plugins/"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_PDB_FILE:${PROJECT_NAME}> "${SKYRIM_MODS_PATH}/${PROJECT_NAME}/SKSE/Plugins/"
        VERBATIM
    )
endif()
