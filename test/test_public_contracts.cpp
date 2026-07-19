/// @file test_public_contracts.cpp
/// @brief Compile-time ownership and framework-portability contracts.

#include <type_traits>

#include "MB85RC/MB85RC.h"

static_assert(!std::is_copy_constructible<::MB85RC::MB85RC>::value,
              "A driver instance must have one explicit owner");
static_assert(!std::is_copy_assignable<::MB85RC::MB85RC>::value,
              "A driver instance must not be copy assigned");
static_assert(!std::is_move_constructible<::MB85RC::MB85RC>::value,
              "Moving a bound driver would invalidate callback ownership");
static_assert(!std::is_move_assignable<::MB85RC::MB85RC>::value,
              "Moving a bound driver would invalidate callback ownership");

static_assert(std::is_standard_layout<::MB85RC::Status>::value,
              "Status must remain a framework-neutral value type");
static_assert(std::is_trivially_copyable<::MB85RC::Status>::value,
              "Status must remain fixed-memory and trivially copyable");
static_assert(std::is_standard_layout<::MB85RC::Config>::value,
              "Config must contain only non-owning framework-neutral values");
static_assert(std::is_trivially_copyable<::MB85RC::Config>::value,
              "Config must not acquire owning or heap-backed fields");
static_assert(std::is_standard_layout<::MB85RC::DeviceId>::value,
              "DeviceId must remain a portable value contract");
static_assert(std::is_trivially_copyable<::MB85RC::DeviceId>::value,
              "DeviceId must remain fixed-memory and trivially copyable");
static_assert(std::is_standard_layout<::MB85RC::TransportResult>::value,
              "Transport results must remain portable terminal values");
static_assert(std::is_trivially_copyable<::MB85RC::TransportResult>::value,
              "Transport results must not own memory or framework state");
static_assert(std::is_standard_layout<::MB85RC::TransferResult>::value,
              "Retained results must be portable values with no caller buffer ownership");
static_assert(std::is_trivially_copyable<::MB85RC::TransferResult>::value,
              "Retained transfer results must remain fixed-memory values");
static_assert(sizeof(::MB85RC::TransportCode) == sizeof(uint8_t),
              "Transport codes are part of a fixed-width adapter contract");
static_assert(sizeof(::MB85RC::WriteCommit) == sizeof(uint8_t),
              "Write disposition must remain fixed-width");
static_assert(sizeof(::MB85RC::TransferKind) == sizeof(uint8_t),
              "Transfer provenance must remain fixed-width");
static_assert(sizeof(::MB85RC::TransferState) == sizeof(uint8_t),
              "Transfer lifecycle must remain fixed-width");
