#include <iostream>
#include <Zydis/Zydis.h>
#include <Windows.h>
#include <filesystem>
#include <fstream>
#include "psapi.h"
#include "ResStruct.hpp"
#include "Patterns.hpp"

#define DEBUG_GAME_PATH "E:\\SteamLibrary\\steamapps\\common\\Soulworker_TWN"


int main(int argc, char** argv)
{

#ifdef _DEBUG
    auto game_dir = std::filesystem::path(DEBUG_GAME_PATH);
    
#else

    if (argc < 2) {
        printf("Invalid params, you must drag and drop SoulWorker64.dll from the game folder to this binary!\n");

        return 0;
    }

    auto file_path = std::filesystem::path(argv[1]);

    if (!std::filesystem::exists(file_path)) {
        printf("Invalid params, file path not found, you must drag and drop SoulWorker64.dll from the game folder to this binary!\n");

        return 0;
    }

    auto game_dir = file_path.parent_path();

#endif

    if (!std::filesystem::exists(game_dir)) {
        printf("Invalid params, game path not found, you must drag and drop SoulWorker64.dll from the game folder to this binary!\n");

        return 0;
    }

    printf("Game directory: %s\n", game_dir.string().c_str());

    wchar_t binary_path_buffer[MAX_PATH];
    GetModuleFileNameW(nullptr, binary_path_buffer, MAX_PATH);

    auto binary_path = std::filesystem::path(binary_path_buffer).parent_path();

    /*We change the working directory so LoadLibrary is able to load all dependencies from the game path,
    it might not even be needed since Windows should set the working directory to the dragged file source folder*/
    std::filesystem::current_path(game_dir);

    //NOTE this is done like this instead of reading the dll file and parsing the DOS header because this way it will work even if the game gets packed in the future

    auto hDll = LoadLibraryW(L"SoulWorker64.dll");

    if (!hDll) {
        printf("Unable to load dll. You must drag and drop SoulWorker64.dll from the game folder to this binary!\n");

        return 0;
    }

    MODULEINFO module_info;
    if (!GetModuleInformation(GetCurrentProcess(), hDll, &module_info, sizeof(MODULEINFO))) {
        printf("Error getting Module info\n");

        return 0;
    }
    auto* module_base = static_cast<uint8_t*>(module_info.lpBaseOfDll);
    auto module_size = module_info.SizeOfImage;

    ZydisDecoder decoder;
    ZydisDecoderInit(&decoder, ZYDIS_MACHINE_MODE_LONG_64, ZYDIS_STACK_WIDTH_64);

    //Change permissions so all regions can be read
    auto* current_address = module_base;
    auto* end_address = current_address + module_size;

    DWORD old_protection;
    if (!VirtualProtect(reinterpret_cast<void*>(current_address), module_size, PAGE_EXECUTE_READWRITE, &old_protection)) {
        printf("Error during VirtualProtect\n");

        return 0;
    }

    //Find all patterns for the read functions
    printf("Finding all patterns...\n");

    if (!Patterns::find_patterns(hDll))
    {
        return 0;
    }

    //Now read find all function calls to figure out structs
    ZydisDecodedOperand operands[ZYDIS_MAX_OPERAND_COUNT];

    char* last_LEA_string = nullptr;

    ResStruct current_struct;

    std::vector<ResStruct> structs;

    printf("Looking for structs in memory...\n");

    while (current_address < end_address) {

        ZydisDecodedInstruction instruction;
       
        if (!ZYAN_SUCCESS(ZydisDecoderDecodeFull(&decoder, current_address, end_address - current_address, &instruction, operands))) {
            ++current_address;
            continue;
        }

        //Store the last found LEA instruction, needed to know the name of the struct
        if (instruction.mnemonic == ZYDIS_MNEMONIC_LEA && instruction.operand_count > 1) {
            ZydisCalcAbsoluteAddress(&instruction, &operands[1], reinterpret_cast<uintptr_t>(current_address), reinterpret_cast<uintptr_t*>(&last_LEA_string));
        }

        //We look for immediate call instructions to detect struct reading
        if (instruction.mnemonic == ZYDIS_MNEMONIC_CALL && operands[0].type == ZYDIS_OPERAND_TYPE_IMMEDIATE) {

            uintptr_t func_addr;

            //We get the address of the called function to figure out what's happening
            if (ZydisCalcAbsoluteAddress(&instruction, &operands[0], reinterpret_cast<uintptr_t>(current_address), &func_addr) == ZYAN_STATUS_SUCCESS) {

                //Start reading function, if it calls this functions it means it will start reading a new struct
                if (func_addr == Patterns::s_read_struct_address && last_LEA_string)
                {
                    ResStruct new_struct;
                    new_struct.m_name = last_LEA_string;
                    current_struct = new_struct;
                }

                //The function to calculate checksum is always called at the end, it means we've read the whole struct
                if (func_addr == Patterns::s_calculate_checksum_address) {
                    structs.push_back(current_struct);
                }

                //If a call to read is hit, we found a new property of the struct
                else if (func_addr == Patterns::s_read_uint8_address) {
                    current_struct.m_properties.push_back(PropertyType::uint8);
                }
                else if (func_addr == Patterns::s_read_int16_address) {
                    current_struct.m_properties.push_back(PropertyType::int16);
                }
                else if (func_addr == Patterns::s_read_uint16_address) {
                    current_struct.m_properties.push_back(PropertyType::uint16);
                }
                else if (func_addr == Patterns::s_read_int32_address) {
                    current_struct.m_properties.push_back(PropertyType::int32);
                }
                else if (func_addr == Patterns::s_read_uint32_address) {
                    current_struct.m_properties.push_back(PropertyType::uint32);
                }
                else if (func_addr == Patterns::s_read_single_address) {
                    current_struct.m_properties.push_back(PropertyType::single);
                }
                else if (func_addr == Patterns::s_read_string_address) {
                    current_struct.m_properties.push_back(PropertyType::string);
                }
            }


        }

        current_address += instruction.length;
    }

    //Once we've read all instructions, we have all structs, we can now save them

    auto now = std::chrono::system_clock::now();
    auto date = std::format("{:%d-%m-%Y %H.%M.%OS}", std::chrono::zoned_time{ std::chrono::current_zone(), now });

    auto structs_folder = binary_path / date;

    std::filesystem::create_directory(structs_folder);

    printf("Dumping structs to files...\n");

    for (auto& res_struct : structs) {
        std::string file_content = "namespace Common.Resources.Tables.Definitions\n{\n";
        file_content += std::format("    public class {} \n    {{\n", res_struct.m_name);

        for (auto i = 0; i < res_struct.m_properties.size(); i++) {
            auto property_type = res_struct.m_properties[i];

            switch (property_type)
            {
            case PropertyType::uint8:
                file_content += std::format("        public byte Unk{} {{ get; set; }}\n", i);
                break;
            case PropertyType::int16:
                file_content += std::format("        public short Unk{} {{ get; set; }}\n", i);
                break;
            case PropertyType::uint16:
                file_content += std::format("        public ushort Unk{} {{ get; set; }}\n", i);
                break;
            case PropertyType::int32:
                file_content += std::format("        public int Unk{} {{ get; set; }}\n", i);
                break;
            case PropertyType::uint32:
                file_content += std::format("        public uint Unk{} {{ get; set; }}\n", i);
                break;
            case PropertyType::single:
                file_content += std::format("        public float Unk{} {{ get; set; }}\n", i);
                break;
            case PropertyType::string:
                file_content += std::format("        public string Unk{} {{ get; set; }}\n", i);
                break;
            default:
                break;
            }
        }

        file_content += "    }\n}";

        auto filename = structs_folder / std::format("{}.cs", res_struct.m_name);

        std::ofstream file(filename);

        if (file.is_open()) {

            file << file_content;

            file.close();
        }
    }

    printf("Dumped %lld structs to %s\n", structs.size(), structs_folder.string().c_str());

    return 0;
}
