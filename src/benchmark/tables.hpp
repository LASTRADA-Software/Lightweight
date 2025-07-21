// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <Lightweight/DataMapper/DataMapper.hpp>
#include <Lightweight/SqlConnection.hpp>
#include <Lightweight/SqlDataBinder.hpp>
#include <Lightweight/SqlQuery.hpp>
#include <Lightweight/SqlQueryFormatter.hpp>
#include <Lightweight/SqlScopedTraceLogger.hpp>
#include <Lightweight/SqlStatement.hpp>
#include <Lightweight/SqlTransaction.hpp>

struct lists;
struct lists_users;
struct movies;
struct ratings;
struct ratings_users;

using Lightweight::Field;
using Lightweight::SqlText;

struct movies final
{
    Field<std::optional<int>> movie_id;
    Field<std::optional<SqlText>> movie_title;
    Field<std::optional<double>> movie_release_year;
    Field<std::optional<SqlText>> movie_url;
    Field<std::optional<SqlText>> movie_title_language;
    Field<std::optional<int>> movie_popularity;
    Field<std::optional<SqlText>> movie_image_url;
    Field<std::optional<SqlText>> director_id;
    Field<std::optional<SqlText>> director_name;
    Field<std::optional<SqlText>> director_url;
};

struct ratings_users final
{
    Field<std::optional<int>> user_id;
    Field<std::optional<SqlText>> rating_date_utc;
    Field<std::optional<int>> user_trialist;
    Field<std::optional<int>> user_subscriber;
    Field<std::optional<SqlText>> user_avatar_image_url;
    Field<std::optional<SqlText>> user_cover_image_url;
    Field<std::optional<int>> user_eligible_for_trial;
    Field<std::optional<int>> user_has_payment_method;
};

struct lists_users final
{
    Field<std::optional<int>> user_id;
    Field<std::optional<int>> list_id;
    Field<std::optional<SqlText>> list_update_date_utc;
    Field<std::optional<SqlText>> list_creation_date_utc;
    Field<std::optional<int>> user_trialist;
    Field<std::optional<int>> user_subscriber;
    Field<std::optional<SqlText>> user_avatar_image_url;
    Field<std::optional<SqlText>> user_cover_image_url;
    Field<std::optional<int>> user_eligible_for_trial;
    Field<std::optional<int>> user_has_payment_method;
};

struct lists final
{
    Field<std::optional<int>> user_id;
    Field<std::optional<int>> list_id;
    Field<std::optional<SqlText>> list_title;
    Field<std::optional<int>> list_movie_number;
    Field<std::optional<SqlText>> list_update_timestamp_utc;
    Field<std::optional<SqlText>> list_creation_timestamp_utc;
    Field<std::optional<int>> list_followers;
    Field<std::optional<SqlText>> list_url;
    Field<std::optional<int>> list_comments;
    Field<std::optional<SqlText>> list_description;
    Field<std::optional<SqlText>> list_cover_image_url;
    Field<std::optional<SqlText>> list_first_image_url;
    Field<std::optional<SqlText>> list_second_image_url;
    Field<std::optional<SqlText>> list_third_image_url;
};

struct ratings final
{
    Field<std::optional<int>> movie_id;
    Field<std::optional<int>> rating_id;
    Field<std::optional<SqlText>> rating_url;
    Field<std::optional<double>> rating_score;
    Field<std::optional<SqlText>> rating_timestamp_utc;
    Field<std::optional<SqlText>> critic;
    Field<std::optional<int>> critic_likes;
    Field<std::optional<int>> critic_comments;
    Field<std::optional<int>> user_id;
    Field<std::optional<int>> user_trialist;
    Field<std::optional<int>> user_subscriber;
    Field<std::optional<int>> user_eligible_for_trial;
    Field<std::optional<int>> user_has_payment_method;
};
