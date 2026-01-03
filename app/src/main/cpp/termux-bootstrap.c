#include <jni.h>
#include <stdint.h>
#include <stddef.h>

extern const unsigned char blob[];
extern const int32_t blob_size;

JNIEXPORT jbyteArray JNICALL Java_com_termux_app_TermuxInstaller_getZip(JNIEnv *env, jclass clazz) {
    (void)clazz;
    if (blob_size <= 0) {
        return NULL;
    }

    jbyteArray ret = (*env)->NewByteArray(env, (jsize)blob_size);
    if (ret == NULL) return NULL;

    (*env)->SetByteArrayRegion(env, ret, 0, (jsize)blob_size, (const jbyte *)blob);
    return ret;
}