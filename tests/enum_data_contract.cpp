#include "cm3d2/enum_data_api.h"

#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

} // namespace

// Behavior observed in DLL_EnumData_* (0x180068600..0x180068710): a dual index
// of number->text and text->number. Add fails when either key is present.
int main() {
    CM3D2_EnumDataHandle data = DLL_EnumData_CreateEnumData();
    require(data != nullptr, "creation returned null");

    require(DLL_EnumData_AddMember(data, 1, "one") == 1, "first add failed");
    require(DLL_EnumData_AddMember(data, 2, "two") == 1, "second add failed");
    require(DLL_EnumData_AddMember(data, 1, "uno") == 0, "duplicate number accepted");
    require(DLL_EnumData_AddMember(data, 3, "one") == 0, "duplicate text accepted");
    require(DLL_EnumData_AddMember(data, 3, "three") == 1, "add after rejected duplicates failed");
    require(DLL_EnumData_AddMember(data, 4, "") == 1, "empty text should be a valid member");
    require(DLL_EnumData_AddMember(data, 5, "") == 0, "duplicate empty text accepted");

    // Removing through one index must free both keys for reuse.
    require(DLL_EnumData_RemoveMenberAtText(data, "one") == 1, "remove by text failed");
    require(DLL_EnumData_RemoveMenberAtText(data, "one") == 0, "second remove by text succeeded");
    require(DLL_EnumData_AddMember(data, 1, "uno") == 1, "number 1 not freed by text removal");
    require(DLL_EnumData_AddMember(data, 9, "one") == 1, "text 'one' not freed by text removal");

    require(DLL_EnumData_RemoveMenberAtNumber(data, 2) == 1, "remove by number failed");
    require(DLL_EnumData_RemoveMenberAtNumber(data, 2) == 0, "second remove by number succeeded");
    require(DLL_EnumData_AddMember(data, 7, "two") == 1, "text 'two' not freed by number removal");
    require(DLL_EnumData_AddMember(data, 2, "dos") == 1, "number 2 not freed by number removal");

    require(DLL_EnumData_RemoveMenberAtNumber(data, 100) == 0, "unknown number removed");
    require(DLL_EnumData_RemoveMenberAtText(data, "absent") == 0, "unknown text removed");
    require(DLL_EnumData_AddMember(data, -5, "negative") == 1, "negative number rejected");
    require(DLL_EnumData_RemoveMenberAtNumber(data, -5) == 1, "negative number removal failed");

    DLL_EnumData_DeleteEnumData(data);
    DLL_EnumData_DeleteEnumData(nullptr); // native checks for null here
    return 0;
}
