#include <new>

#include "jni_internals.h"

namespace {

class JavaFileDescriptor : public Object {
public:
    static Class clazz;
    jint descriptor = -1;

    Class *_getClass() override { return &clazz; }
};

static void file_descriptor_ctor(JNIEnv *, jobject obj, jclass)
{
    if (obj)
        new (obj) JavaFileDescriptor();
}

static const ManagedMethod methods[] = {
    ManagedMethod::RegisterNonVirtual<&file_descriptor_ctor>(
        JavaFileDescriptor::clazz, "<init>", "()V"),
    {nullptr},
};

static const FieldId fields[] = {
    {
        .clazz = &JavaFileDescriptor::clazz,
        .name = "descriptor",
        .signature = "I",
        .offset = (uintptr_t)&(((JavaFileDescriptor *)0)->descriptor),
        .is_static = 0,
    },
    {},
};

Class JavaFileDescriptor::clazz = {
    .classpath = "java/io/FileDescriptor",
    .classname = "FileDescriptor",
    .managed_methods = methods,
    .native_methods = nullptr,
    .fields = fields,
    .instance_size = sizeof(JavaFileDescriptor),
};

const int registered =
    ClassRegistry::register_class(JavaFileDescriptor::clazz);

} // namespace

extern "C" int open_citadel_file_descriptor_get(jobject object)
{
    if (!object)
        return -1;
    auto *fd = reinterpret_cast<JavaFileDescriptor *>(object);
    if (fd->_getClass() != &JavaFileDescriptor::clazz)
        return -1;
    return fd->descriptor;
}
