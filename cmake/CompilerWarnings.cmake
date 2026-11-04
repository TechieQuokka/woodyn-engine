# Warning flags applied to WOODYN's own targets only (never to third-party code).
function(woodyn_set_warnings target)
    set(flags
        -Wall
        -Wextra
        -Wpedantic
        -Wshadow
        -Wconversion
        -Wsign-conversion
        -Wnon-virtual-dtor
        -Wold-style-cast
        -Woverloaded-virtual
        -Wnull-dereference
        -Wdouble-promotion
    )
    if(WOODYN_WARNINGS_AS_ERRORS)
        list(APPEND flags -Werror)
    endif()
    target_compile_options(${target} PRIVATE ${flags})
endfunction()
