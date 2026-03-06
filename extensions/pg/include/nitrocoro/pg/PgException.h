/**
 * @file PgException.h
 * @brief PostgreSQL exception hierarchy
 */
#pragma once

#include <stdexcept>
#include <string>

namespace nitrocoro::pg
{

class PgException : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
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

class PgTimeoutError : public PgException
{
public:
    using PgException::PgException;
};

} // namespace nitrocoro::pg
