#include <jni.h>
#include <stdint.h>

extern const unsigned char blob[] __attribute__((visibility("hidden")));
extern const int blob_size __attribute__((visibility("hidden")));

JNIEXPORT jbyteArray JNICALL Java_com_termux_app_TermuxInstaller_getZip(JNIEnv *env, jobject This) {
    (void)This;
    jsize size = (jsize)blob_size;
    jbyteArray ret = (*env)->NewByteArray(env, size);
    if (ret == NULL) return NULL;
    (*env)->SetByteArrayRegion(env, ret, 0, size, (const jbyte *)blob);
    return ret;
}