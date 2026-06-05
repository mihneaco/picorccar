function(generate_uuid_v4 p_output_var)
    set(uuid_hex_alphabet "0123456789ABCDEF")
    set(uuid_variant_alphabet "89AB")

    string(RANDOM LENGTH 8 ALPHABET "${uuid_hex_alphabet}" uuid_group_0)
    string(RANDOM LENGTH 4 ALPHABET "${uuid_hex_alphabet}" uuid_group_1)
    string(RANDOM LENGTH 3 ALPHABET "${uuid_hex_alphabet}" uuid_group_2_tail)
    string(RANDOM LENGTH 1 ALPHABET "${uuid_variant_alphabet}" uuid_variant)
    string(RANDOM LENGTH 3 ALPHABET "${uuid_hex_alphabet}" uuid_group_3_tail)
    string(RANDOM LENGTH 12 ALPHABET "${uuid_hex_alphabet}" uuid_group_4)

    set(${p_output_var}
        "${uuid_group_0}-${uuid_group_1}-4${uuid_group_2_tail}-${uuid_variant}${uuid_group_3_tail}-${uuid_group_4}"
        PARENT_SCOPE)
endfunction()

function(set_generated_uuid p_cache_var p_description)
    if (DEFINED ${p_cache_var})
        set(current_uuid "${${p_cache_var}}")
    else()
        set(current_uuid "")
    endif()

    if (current_uuid STREQUAL "")
        generate_uuid_v4(generated_uuid)
        set(${p_cache_var} "${generated_uuid}" CACHE STRING "${p_description}" FORCE)
    endif()
endfunction()

function(uuid_to_c_bytes p_uuid p_output_var)
    set(uuid_string_length 36)
    set(uuid_expected_hex_length 32)
    set(uuid_byte_count 16)
    set(uuid_hex_digits_per_byte 2)

    string(LENGTH "${p_uuid}" uuid_length)
    if (NOT uuid_length EQUAL uuid_string_length)
        message(FATAL_ERROR "Invalid UUID '${p_uuid}'")
    endif()

    string(SUBSTRING "${p_uuid}" 8 1 uuid_hyphen_0)
    string(SUBSTRING "${p_uuid}" 13 1 uuid_hyphen_1)
    string(SUBSTRING "${p_uuid}" 18 1 uuid_hyphen_2)
    string(SUBSTRING "${p_uuid}" 23 1 uuid_hyphen_3)
    if (NOT uuid_hyphen_0 STREQUAL "-"
        OR NOT uuid_hyphen_1 STREQUAL "-"
        OR NOT uuid_hyphen_2 STREQUAL "-"
        OR NOT uuid_hyphen_3 STREQUAL "-")
        message(FATAL_ERROR "Invalid UUID '${p_uuid}'")
    endif()

    string(REPLACE "-" "" uuid_hex "${p_uuid}")
    string(LENGTH "${uuid_hex}" uuid_hex_length)
    if (NOT uuid_hex_length EQUAL uuid_expected_hex_length OR uuid_hex MATCHES "[^0-9A-Fa-f]")
        message(FATAL_ERROR "Invalid UUID '${p_uuid}'")
    endif()

    string(TOLOWER "${uuid_hex}" uuid_hex)

    set(uuid_bytes)
    math(EXPR last_byte_index "${uuid_byte_count} - 1")
    foreach(byte_index RANGE 0 ${last_byte_index})
        math(EXPR byte_offset "${byte_index} * ${uuid_hex_digits_per_byte}")
        string(SUBSTRING "${uuid_hex}" ${byte_offset} ${uuid_hex_digits_per_byte} byte_hex)
        list(APPEND uuid_bytes "0x${byte_hex}")
    endforeach()

    string(JOIN ", " uuid_initializer ${uuid_bytes})
    set(${p_output_var} "${uuid_initializer}" PARENT_SCOPE)
endfunction()
