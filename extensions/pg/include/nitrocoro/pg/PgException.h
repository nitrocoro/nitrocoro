/**
 * @file PgException.h
 * @brief PostgreSQL exception hierarchy
 */
#pragma once

#include <stdexcept>
#include <string>

namespace nitrocoro::pg
{

class PgException : public std::exception
{
public:
    explicit PgException(std::string msg, std::string sqlstate = {})
        : msg_(std::move(msg)), sqlstate_(std::move(sqlstate)) {}
    const char * what() const noexcept override { return msg_.c_str(); }
    std::string_view sqlstate() const { return sqlstate_; }

private:
    std::string msg_;
    std::string sqlstate_;
};

class PgConnectionError : public PgException
{
public:
    using PgException::PgException;
};

class PgQueryError : public PgException
{
public:
    using PgException::PgException;
};

class PgTransactionError : public PgException
{
public:
    using PgException::PgException;
};

class PgCancelledError : public PgException
{
public:
    using PgException::PgException;
};

class PgTimeoutError : public PgCancelledError
{
public:
    using PgCancelledError::PgCancelledError;
};

} // namespace nitrocoro::pg
