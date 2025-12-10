// Tool to embed binary files as C++ headers
#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <format>
#include <iostream>
#include <iterator>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

using namespace std::literals;

static constexpr std::string make_header_guard(std::string_view varname)
{
    std::string out = "EMBEDDED_";
    out.reserve(out.size() + varname.size() + 2);
    for (char c : varname)
    {
        if (c >= 'a' && c <= 'z') out.push_back(char(c - 'a' + 'A'));
        else if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) out.push_back(c);
        else out.push_back('_');
    }
    out += "_H";
    return out;
}

int main(int argc, char* argv[])
{
    try
    {
        if (argc != 4)
        {
            std::cerr << "Usage: " << argv[0] << " <input_file> <output_file> <variable_name>\n";
            return 1;
        }

        const std::filesystem::path input_path = argv[1];
        const std::filesystem::path output_path = argv[2];
        const std::string var_name = argv[3];

        if (var_name.empty())
        {
            std::cerr << "Error: variable_name must be non-empty\n";
            return 2;
        }

        // Read file into buffer
        std::ifstream ifs(input_path, std::ios::binary | std::ios::ate);
        if (!ifs)
        {
            std::error_code ec(errno, std::generic_category());
            std::cerr << std::format("Error: cannot open input file '{}': {}\n", input_path.string(), ec.message());
            return 3;
        }

        const auto file_size = static_cast<size_t>(ifs.tellg());
        ifs.seekg(0, std::ios::beg);

        std::vector<std::byte> data;
        data.resize(file_size);
        if (!ifs.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(file_size)))
        {
            std::cerr << "Error: failed to read input file\n";
            return 4;
        }
        ifs.close();

        // Prepare header contents
        const std::string filename = input_path.filename().string();

        std::ofstream ofs(output_path, std::ios::trunc);
        if (!ofs)
        {
            std::error_code ec(errno, std::generic_category());
            std::cerr << std::format("Error: cannot create output file '{}': {}\n", output_path.string(), ec.message());
            return 5;
        }

        ofs << std::format("// Auto-generated from {}\n", filename);
        ofs << "#pragma once\n\n";
        ofs << "#include <cstdint>\n#include <cstddef>\n\n";
        ofs << std::format("inline constexpr unsigned char {}[] = {{\n", var_name);

        // Write bytes 12 per line
        for (size_t i = 0; i < data.size(); ++i)
        {
            constexpr size_t per_line = 12;
            if (i % per_line == 0) ofs << "    ";
            const unsigned val = static_cast<unsigned>(std::to_integer<int>(data[i])) & 0xFFu;
            ofs << std::format("0x{:02X}", val);
            if (i + 1 < data.size())
            {
                ofs << ',';
                if ((i + 1) % per_line == 0) ofs << '\n';
                else ofs << ' ';
            }
        }

        ofs << "\n};\n\n";
        ofs << std::format("inline constexpr std::size_t {}_len = {};\n", var_name, data.size());

        ofs.close();

        std::cout << std::format("Generated {} ({} bytes)\n", output_path.string(), data.size());
        return 0;
    }
    catch (const std::exception& ex)
    {
        std::cerr << "Unhandled exception: " << ex.what() << '\n';
        return 99;
    }
}
