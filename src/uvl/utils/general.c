
#include <stdio.h>
#include <string.h>
#include "utils/general.h"

bool streq(const char *str1, const char *str2){
    if (strcmp(str1, str2) == 0){
        return true;
    }
    return false;
}

// void slice_string(char *str, size_t i, size_t j){
//     str[j] = '\0';
//     str[0] = str[0] + i;
// }

// char *replace_string(char *str, char *matched_str, char *replaced_str) {
//     size_t sub_str_len = strlen(sub_str);

//     char *copied_sub_str;

//     size_t i = 0, k = 0, found = 0;
//     while (str[i] != '\0'){
//         if (matched_str[k] == str[i]){
//             k++;
//         }else{
//             k = 0;
//         }

//         if (matched_str[k] == '\0'){
//             strcpy(copied_sub_str, str);
//             for (size_t j = k; j > 0; j--){

//             }
//             k = 0;
//         }

//         i++;
//     }

// }