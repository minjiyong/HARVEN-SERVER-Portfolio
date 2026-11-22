#include "pch.h"
#include "Base/Core/LocalEnv.h"

#include <cstdlib>
#include <fstream>

namespace
{
    namespace fs = std::filesystem;

    std::string Trim(const std::string& text)
    {
        const size_t first = text.find_first_not_of(" \t");
        if (first == std::string::npos)
            return {};
        const size_t last = text.find_last_not_of(" \t");
        return text.substr(first, last - first + 1);
    }

    // GameData::ResolveDir와 같은 순서 — 실행 파일 폴더, 그다음 작업 폴더에서 위로 6단까지
    fs::path FindEnvFile(const char* argv0)
    {
        if (const char* explicitPath = std::getenv("HARVEN_ENV_FILE"); explicitPath != nullptr && *explicitPath != '\0')
            return fs::path(explicitPath);

        std::error_code ec;
        std::vector<fs::path> starts;
        if (argv0 != nullptr && *argv0 != '\0')
            starts.push_back(fs::absolute(fs::path(argv0), ec).lexically_normal().parent_path());
        starts.push_back(fs::current_path(ec));
        for (const auto& start : starts)
        {
            fs::path p = start;
            for (int up = 0; up < 6 && !p.empty(); ++up)
            {
                if (fs::is_regular_file(p / CLocalEnv::kFileName, ec))
                    return p / CLocalEnv::kFileName;
                const fs::path parent = p.parent_path();
                if (parent == p)
                    break;
                p = parent;
            }
        }
        return {};
    }
}

fs::path CLocalEnv::Load(const char* argv0)
{
    const fs::path file = FindEnvFile(argv0);
    if (file.empty())
        return {};   // 없으면 조용히 — 운영은 환경 변수로 준다

    std::ifstream in(file, std::ios::binary);
    if (!in)
    {
        std::cerr << "[Env] cannot read " << file.string() << std::endl;
        return {};
    }

    std::error_code ec;
    const fs::path baseDir = fs::absolute(file, ec).parent_path();
    std::vector<std::string> applied, kept;
    std::string line;
    int lineNo = 0;
    while (std::getline(in, line))
    {
        ++lineNo;
        if (lineNo == 1 && line.rfind("\xEF\xBB\xBF", 0) == 0)
            line.erase(0, 3);   // 메모장이 붙이는 UTF-8 BOM
        if (!line.empty() && line.back() == '\r')
            line.pop_back();    // Windows에서 고친 파일(CRLF)
        line = Trim(line);
        if (line.empty() || line[0] == '#')
            continue;
        if (line.rfind("export ", 0) == 0)
            line = Trim(line.substr(7));

        const size_t eq = line.find('=');
        if (eq == std::string::npos)
        {
            std::cerr << "[Env] " << file.filename().string() << ":" << lineNo << " is not KEY=VALUE - skipped" << std::endl;
            continue;
        }
        const std::string key = Trim(line.substr(0, eq));
        std::string value = Trim(line.substr(eq + 1));
        if (value.size() >= 2 && (value.front() == '"' || value.front() == '\'') && value.back() == value.front())
            value = value.substr(1, value.size() - 2);
        if (key.empty() || value.empty())
            continue;

        if (std::getenv(key.c_str()) != nullptr)
        {
            kept.push_back(key);
            continue;
        }
        if (key == "HARVEN_DB_CA" && fs::path(value).is_relative())
            value = (baseDir / value).lexically_normal().string();
        ::setenv(key.c_str(), value.c_str(), 0);
        applied.push_back(key);
    }

    std::cout << "[Env] " << file.string() << ":";
    for (const auto& key : applied)
        std::cout << " " << key;
    if (applied.empty())
        std::cout << " (nothing set)";
    if (!kept.empty())
    {
        std::cout << " | kept from process env:";
        for (const auto& key : kept)
            std::cout << " " << key;
    }
    std::cout << std::endl;
    return file;
}
