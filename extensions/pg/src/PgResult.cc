#include "nitrocoro/pg/PgResult.h"

#include <libpq-fe.h>

namespace nitrocoro::pg
{

PgResult::PgResult(std::shared_ptr<PGresult> res)
    : res_(std::move(res))
    , rows_(res_ ? static_cast<size_t>(PQntuples(res_.get())) : 0)
    , cols_(res_ ? static_cast<size_t>(PQnfields(res_.get())) : 0)
{
}

const char * PgResult::colName(size_t col) const
{
    if (col >= cols_)
        return nullptr;
    return PQfname(res_.get(), static_cast<int>(col));
}

PgValue PgResult::get(size_t row, size_t col) const
{
    if (row >= rows_ || col >= cols_)
        return std::monostate{};

    int r = static_cast<int>(row);
    int c = static_cast<int>(col);

    if (PQgetisnull(res_.get(), r, c))
        return std::monostate{};

    const char * val = PQgetvalue(res_.get(), r, c);
    Oid oid = PQftype(res_.get(), c);

    switch (oid)
    {
        case 16: // bool
            return val[0] == 't';
        case 20: // int8
        case 21: // int2
        case 23: // int4
            return static_cast<int64_t>(std::stoll(val));
        case 700:  // float4
        case 701:  // float8
        case 1700: // numeric
            return std::stod(val);
        case 17: // bytea — libpq returns hex-escaped by default
        {
            size_t len = 0;
            unsigned char * decoded = PQunescapeBytea(reinterpret_cast<const unsigned char *>(val), &len);
            std::vector<uint8_t> bytes(decoded, decoded + len);
            PQfreemem(decoded);
            return bytes;
        }
        default:
            return std::string(val);
    }
}

} // namespace nitrocoro::pg
