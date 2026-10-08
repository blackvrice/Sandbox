# SandboxNetwork — Transport · Protocol · ServerHost · ClientSession (docs/08-NETWORK.md, ADR-0024).
# 의존: Core (+ Foundation). Platform · Render · Editor 를 모른다 — SandboxServer 가 이것과 Core 만으로 돈다.
add_library(SandboxNetwork STATIC
    transport/Transport.hpp
    transport/Transport.cpp
    transport/LoopbackTransport.hpp
    transport/LoopbackTransport.cpp
    transport/SimulatedTransport.hpp
    transport/SimulatedTransport.cpp
    transport/EnetTransport.hpp
    transport/EnetTransport.cpp

    protocol/BitStream.hpp
    protocol/BitStream.cpp
    protocol/CommandCodec.hpp
    protocol/CommandCodec.cpp
    protocol/Messages.hpp
    protocol/Messages.cpp
)
add_library(Sandbox::Network ALIAS SandboxNetwork)
target_link_libraries(SandboxNetwork
    PUBLIC  SandboxCore
    PRIVATE sbx_enet                  # enet.h 는 transport/EnetTransport.cpp 안에서만 (check_includes.py)
            sbx_warnings
            sbx_strict_conversions)
