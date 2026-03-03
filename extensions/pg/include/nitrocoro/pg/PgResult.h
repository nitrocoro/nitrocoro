/**
 * @file PgResult.h
 * @brief PostgreSQL query result
 */
#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

struct pg_result;
typedef struct pg_result PGresult;

namespace nitrocoro::pg
{

using PgValue = std::variant<std::monostate, bool, int64_t, double, std::string, std::vector<uint8_t>>;

class PgResult
{
public:
    PgResult() = default;
    explicit PgResult(std::shared_ptr<PGresult> res);

    size_t rowCount() const { return rows_; }
    size_t colCount() const { return cols_; }
    const char * colName(size_t col) const;

    PgValue get(size_t row, size_t col) const;

private:
    std::shared_ptr<PGresult> res_;
    size_t rows_ = 0;
    size_t cols_ = 0;
};

} // namespace nitrocoro::pg
