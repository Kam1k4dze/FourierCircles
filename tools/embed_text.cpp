// Tool to embed text files as C++ headers
#include <cerrno>
#include <filesystem>
#include <fstream>
#include <format>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>


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

        // Read entire text file
        std::ifstream ifs(input_path);
        if (!ifs)
        {
            std::error_code ec(errno, std::generic_category());
            std::cerr << std::format("Error: cannot open input file '{}': {}\n", input_path.string(), ec.message());
            return 3;
        }
        std::ostringstream ss;
        ss << ifs.rdbuf();
        const std::string text = ss.str();
        ifs.close();

        const std::string filename = input_path.filename().string();

        std::ofstream ofs(output_path, std::ios::trunc);
        if (!ofs)
        {
            std::error_code ec(errno, std::generic_category());
            std::cerr << std::format("Error: cannot create output file '{}': {}\n", output_path.string(), ec.message());
            return 4;
        }

        ofs << std::format("// Auto-generated from {}\n", filename);
        ofs << "#pragma once\n\n";
        ofs << "#include <string_view>\n\n";

        // Use raw string literal with a unique delimiter derived from variable name
        std::string delim = "TXT_CONTENT";
        // If var_name may contain characters that could collide, keep delim fixed; it's very unlikely to appear.
        ofs << std::format("inline constexpr std::string_view {} = R\"{}(\n", var_name, delim);
        ofs << text;
        ofs << std::format("){}\";\n", delim);

        ofs.close();

        std::cout << std::format("Generated {} ({} characters)\n", output_path.string(), text.size());
        return 0;
    }
    catch (const std::exception& ex)
    {
        std::cerr << "Unhandled exception: " << ex.what() << '\n';
        return 99;
    }
}
