#ifndef BGM_IL2CPP_API_H
#define BGM_IL2CPP_API_H
static const char *API_NAMES[BGM_API_COUNT]={
 "il2cpp_domain_get",
 "il2cpp_domain_get_assemblies",
 "il2cpp_assembly_get_image",
 "il2cpp_class_from_name",
 "il2cpp_class_get_method_from_name",
 "il2cpp_class_get_field_from_name",
 "il2cpp_field_get_offset",
 "il2cpp_method_get_param_count",
 "il2cpp_method_get_return_type",
 "il2cpp_method_get_param",
 "il2cpp_type_get_name",
 "il2cpp_free",
 "il2cpp_method_get_flags",
 "il2cpp_field_get_type",
 "il2cpp_type_get_type",
 "il2cpp_resolve_icall",
 "il2cpp_class_get_parent",
 "il2cpp_field_static_get_value",
 "il2cpp_runtime_invoke",
 "il2cpp_object_unbox",
 "il2cpp_object_get_class",
 "il2cpp_object_get_virtual_method",
 "il2cpp_gchandle_new",
 "il2cpp_gchandle_free",
 "il2cpp_gchandle_get_target",
 "il2cpp_class_is_assignable_from",
 "il2cpp_thread_attach",
 "il2cpp_thread_detach",
 "il2cpp_field_get_value",
 "il2cpp_field_set_value",
 "il2cpp_string_new",
 "il2cpp_object_new",
 "il2cpp_class_get_methods",
 "il2cpp_method_get_name"
};
typedef struct {
 void *(*domain)(void); void **(*assemblies)(void *,size_t *); void *(*image)(void *);
 void *(*klass)(void *,const char *,const char *); void *(*method)(void *,const char *,int);
 void *(*field)(void *,const char *); size_t (*offset)(void *);
 uint32_t (*argc)(const void *); void *(*result)(const void *); void *(*param)(const void *,uint32_t);
 char *(*type_name)(const void *); void (*free_mem)(void *); uint32_t (*flags)(const void *,uint32_t *);
 void *(*field_type)(void *); int (*type_kind)(const void *); void *(*icall)(const char *);
 void *(*parent)(void *); void (*static_value)(void *,void *);
 void *(*invoke)(const void *,void *,void **,void **); void *(*unbox)(void *);
 void *(*object_class)(void *); void *(*virtual_method)(void *,const void *);
 /* Unity 6 GC handles are pointer-sized on the exported C API. Preserve the
  * full value when retaining or releasing battle stage objects. */
 uintptr_t (*root)(void *,bool); void (*unroot)(uintptr_t); void *(*root_target)(uintptr_t);
 bool (*assignable)(void *,void *); void *(*attach)(void *); void (*detach)(void *);
 void (*read)(void *,void *,void *); void (*write)(void *,void *,void *);
 void *(*string_new)(const char *);
 void *(*object_new)(void *);
 const void *(*methods)(void *,void **);
 const char *(*method_name)(const void *);
} API;
#endif
