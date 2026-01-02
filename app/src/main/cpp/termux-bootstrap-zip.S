.section .rodata
.align 8
.global blob
.global blob_size
.type blob, %object
.type blob_size, %object

blob:
#if defined(__i386__) || defined(__i686__)
.incbin "bootstrap-i686.zip"
#elif defined(__x86_64__)
.incbin "bootstrap-x86_64.zip"
#elif defined(__aarch64__)
.incbin "bootstrap-aarch64.zip"
#elif defined(__arm__) || defined(__thumb__)
.incbin "bootstrap-arm.zip"
#else
# error "Unsupported arch"
#endif

1:
.size blob, .-blob
.align 4
blob_size:
    .int 1b - blob
.size blob_size, 4

.section .note.GNU-stack,"",%progbits
