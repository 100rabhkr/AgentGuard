# Sanitizers.cmake
# Adds AddressSanitizer, ThreadSanitizer, or UBSan to a target

function(target_apply_sanitizers TARGET)
    if(AGENTGUARD_ENABLE_ASAN)
        target_compile_options(${TARGET} PRIVATE -fsanitize=address -fno-omit-frame-pointer)
        target_link_options(${TARGET} PRIVATE -fsanitize=address)
    endif()

    if(AGENTGUARD_ENABLE_TSAN)
        target_compile_options(${TARGET} PRIVATE -fsanitize=thread)
        target_link_options(${TARGET} PRIVATE -fsanitize=thread)
    endif()

    if(AGENTGUARD_ENABLE_UBSAN)
        target_compile_options(${TARGET} PRIVATE -fsanitize=undefined)
        target_link_options(${TARGET} PRIVATE -fsanitize=undefined)
    endif()
endfunction()
