// Reconstructed interpreter only. No original execution or modal UI.
#include "menu_database.h"
#include "enum_data_internal.h"
#include <cstdio>
#include <stdexcept>
using namespace cm3d2::internal;
void require(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
int main() {
    try {
        EnumData categories,colors;
        for (const char* command : {"name","setumei","category","color_set","icon","icons","saveitem","priority","\xe3\x83\xa1\xe3\x83\x8b\xe3\x83\xa5\xe3\x83\xbc\xe3\x83\x95\xe3\x82\xa9\xe3\x83\xab\xe3\x83\x80"}) {
            MenuDocument doc; doc.commands={{command}};
            MenuFields fields;fields.category_mpn_text="kept";
            bool threw=false;
            try { interpretMenuCommands(doc,categories,colors,fields); }
            catch (const std::exception& e) {threw=std::string(e.what())=="invalid vector subscript";}
            require(threw,"missing argument reaches native-style error handler");
            require(fields.category_mpn_text=="kept","persistent category preserved on error");
        }
        for (auto command : {std::vector<std::string>{},std::vector<std::string>{"name",""}}) {
            MenuDocument doc;doc.commands={command};MenuFields fields;
            bool threw=false;
            try {interpretMenuCommands(doc,categories,colors,fields);}
            catch (const std::exception& e) {threw=std::string(e.what())=="invalid vector subscript";}
            require(threw,"empty command/name split error");
        }
        MenuDocument doc;doc.commands={{"unknown"},{"unsetitem"},{"collabo"},{"icon","ok.tex"}};
        MenuFields fields;
        require(interpretMenuCommands(doc,categories,colors,fields) && fields.bo_del_only && fields.is_collabo && fields.icon_s=="ok.tex",
            "legitimate zero-argument commands and unknown commands retained");
        std::puts("menu_command_error_contract: ok");return 0;
    } catch (const std::exception& e) {std::fprintf(stderr,"%s\n",e.what());return 1;}
}
