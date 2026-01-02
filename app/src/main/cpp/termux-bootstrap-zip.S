.global blob
    .global blob_size
    .section .rodata
blob:
#if defined(__i386__) || defined(__i686__) || defined(i686)
    .incbin "bootstrap-i686.zip"
#elif defined(__x86_64__) || defined(x86_64)
    .incbin "bootstrap-x86_64.zip"
#elif defined(__aarch64__) || defined(aarch64)
    .incbin "bootstrap-aarch64.zip"
#elif defined(__arm__) || defined(arm)
    .incbin "bootstrap-arm.zip"
#else
#error Unsupported arch
#endif
1:
blob_size:
    .int 1b - blob