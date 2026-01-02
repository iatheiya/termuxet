#include <jni.h>

extern const char blob[];
extern const int blob_size;

JNIEXPORT jbyteArray JNICALL Java_com_termux_app_TermuxInstaller_getZip(JNIEnv *env, jobject This)
{
    jsize size = (jsize)blob_size;
    jbyteArray ret = (*env)->NewByteArray(env, size);
    if (ret == NULL) return NULL;
    (*env)->SetByteArrayRegion(env, ret, 0, size, (const jbyte *)blob);
    return ret;
}