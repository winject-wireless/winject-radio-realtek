#include "PowerCal.h"

#include <cmath>
#include <cstdlib>
#include <fstream>

namespace winject
{

namespace
{

std::string trim(const std::string& s)
{
    const auto a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos)
    {
        return "";
    }
    const auto b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

bool parse_double(const std::string& text, double* out)
{
    const std::string t = trim(text);
    if (t.empty())
    {
        return false;
    }
    char* end = nullptr;
    *out = strtod(t.c_str(), &end);
    return *end == '\0' && std::isfinite(*out);
}

}  // namespace

bool PowerCal::load_csv(const std::string& path, std::string* error)
{
    std::ifstream in(path);
    if (!in.is_open())
    {
        *error = "cannot open " + path;
        return false;
    }
    dbm = {};
    have = {};
    int count = 0;
    int line_no = 0;
    std::string line;
    while (std::getline(in, line))
    {
        ++line_no;
        const std::string t = trim(line);
        if (t.empty() || t[0] == '#')
        {
            continue;
        }
        const auto comma = t.find(',');
        double idx = 0;
        double value = 0;
        const bool idx_ok =
            comma != std::string::npos && parse_double(t.substr(0, comma), &idx);
        if (!idx_ok && count == 0 && line_no == 1)
        {
            continue;  // header
        }
        const std::string where = path + ":" + std::to_string(line_no);
        if (!idx_ok || !parse_double(t.substr(comma + 1), &value) ||
            idx != std::floor(idx) || idx < 0 || idx > k_max_idx)
        {
            *error = where + ": expected idx,tx_power with idx 0-63";
            return false;
        }
        const int n = static_cast<int>(idx);
        if (n < k_min_idx)
        {
            continue;  // idx 0 is the driver default, never used
        }
        if (have[n])
        {
            *error = where + ": duplicate idx " + std::to_string(n);
            return false;
        }
        dbm[n] = value;
        have[n] = true;
        ++count;
    }
    if (count < 2)
    {
        *error = path + ": need at least 2 rows with idx 1-63";
        return false;
    }
    return true;
}

int PowerCal::dbm_to_idx(int dbm, bool* clamped) const
{
    int best = -1;
    double best_err = 0;
    double lo = 0;
    double hi = 0;
    for (int i = k_min_idx; i <= k_max_idx; ++i)
    {
        if (!have[i])
        {
            continue;
        }
        const double err = std::fabs(this->dbm[i] - dbm);
        if (best < 0)
        {
            lo = hi = this->dbm[i];
        }
        if (best < 0 || err < best_err)
        {
            best = i;
            best_err = err;
        }
        lo = std::fmin(lo, this->dbm[i]);
        hi = std::fmax(hi, this->dbm[i]);
    }
    if (clamped != nullptr)
    {
        *clamped = best < 0 || dbm < lo - 0.5 || dbm > hi + 0.5;
    }
    return best < 0 ? k_min_idx : best;
}

}  // namespace winject
