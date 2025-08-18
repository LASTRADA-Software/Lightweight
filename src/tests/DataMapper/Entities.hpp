// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <Lightweight/Lightweight.hpp>

#include <ostream>

struct Person
{
    Light::Field<Light::SqlGuid, Light::PrimaryKey::AutoAssign> id;
    Light::Field<Light::SqlAnsiString<25>> name;
    Light::Field<bool> is_active { true };
    Light::Field<std::optional<int>> age;

    std::weak_ordering operator<=>(Person const& other) const = default;
};

// This is a test to only partially query a table row (a few columns)
struct PersonName
{
    Light::Field<Light::SqlGuid, Light::PrimaryKey::AutoAssign> id;
    Light::Field<Light::SqlAnsiString<25>> name;

    static constexpr std::string_view TableName = Light::RecordTableName<Person>;
};

inline std::ostream& operator<<(std::ostream& os, Person const& value)
{
    return os << std::format("Person {{ id: {}, name: {}, is_active: {}, age: {} }}",
                             value.id.Value(),
                             value.name.Value(),
                             value.is_active.Value(),
                             value.age.Value().value_or(-1));
}

struct User;
struct Email;

struct User
{
    Light::Field<Light::SqlGuid, Light::PrimaryKey::AutoAssign> id {};
    Light::Field<Light::SqlAnsiString<30>> name {};

    Light::HasMany<Email> emails {};
};

struct Email
{
    Light::Field<Light::SqlGuid, Light::PrimaryKey::AutoAssign> id {};
    Light::Field<Light::SqlAnsiString<30>> address {};
    Light::BelongsTo<&User::id, Light::SqlRealName { "user_id" }> user {};

    constexpr std::weak_ordering operator<=>(Email const& other) const = default;
};

struct Physician;
struct Appointment;
struct Patient;

struct Physician
{
    Light::Field<Light::SqlGuid, Light::PrimaryKey::AutoAssign> id;
    Light::Field<Light::SqlAnsiString<30>> name;
    Light::HasMany<Appointment> appointments;
    Light::HasManyThrough<Patient, Appointment> patients;

    constexpr std::weak_ordering operator<=>(Physician const& other) const
    {
        if (auto result = id.Value() <=> other.id.Value(); result != std::weak_ordering::equivalent)
            return result;

        if (auto result = name.Value() <=> other.name.Value(); result != std::weak_ordering::equivalent)
            return result;

        return std::weak_ordering::equivalent;
    }
};

struct Patient
{
    Light::Field<Light::SqlGuid, Light::PrimaryKey::AutoAssign> id;
    Light::Field<Light::SqlAnsiString<30>> name;
    Light::Field<Light::SqlAnsiString<30>> comment;
    Light::HasMany<Appointment> appointments;
    Light::HasManyThrough<Physician, Appointment> physicians;

    constexpr std::weak_ordering operator<=>(Patient const& other) const
    {
        if (auto result = id.Value() <=> other.id.Value(); result != std::weak_ordering::equivalent)
            return result;

        if (auto result = name.Value() <=> other.name.Value(); result != std::weak_ordering::equivalent)
            return result;

        if (auto result = comment.Value() <=> other.comment.Value(); result != std::weak_ordering::equivalent)
            return result;

        return std::weak_ordering::equivalent;
    }
};

struct Appointment
{
    Light::Field<Light::SqlGuid, Light::PrimaryKey::AutoAssign> id;
    Light::Field<Light::SqlDateTime> date;
    Light::Field<Light::SqlAnsiString<80>> comment;
    Light::BelongsTo<&Physician::id, Light::SqlRealName { "physician_id" }> physician;
    Light::BelongsTo<&Patient::id, Light::SqlRealName { "patient_id" }> patient;

    constexpr std::weak_ordering operator<=>(Appointment const& other) const
    {
        if (auto const result = id.Value() <=> other.id.Value(); result != std::weak_ordering::equivalent)
            return result;

        if (auto const result = date.Value() <=> other.date.Value(); result != std::weak_ordering::equivalent)
            return result;

        if (auto const result = comment.Value() <=> other.comment.Value(); result != std::weak_ordering::equivalent)
            return result;

        if (auto const result = physician.Value() <=> other.physician.Value(); result != std::weak_ordering::equivalent)
            return result;

        if (auto const result = patient.Value() <=> other.patient.Value(); result != std::weak_ordering::equivalent)
            return result;

        return std::weak_ordering::equivalent;
    }
};
