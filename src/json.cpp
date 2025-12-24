#include "json.hpp"
#include "yyjson.h"
#include <vector>

namespace lite3 {
    namespace lite3_json {

        // Forward declarations for helper functions
        void from_yyjson_val(yyjson_val *val, Buffer& buffer, size_t ofs);
        void from_yyjson_val(yyjson_val *val, Buffer& buffer, size_t ofs, const char* key);
        yyjson_mut_val* to_yyjson_val(const Buffer& buffer, size_t ofs, yyjson_mut_doc *doc);

        std::string to_json_string(const Buffer& buffer, size_t ofs) {
            yyjson_mut_doc *doc = yyjson_mut_doc_new(nullptr);
            yyjson_mut_val *root = to_yyjson_val(buffer, ofs, doc);
            yyjson_mut_doc_set_root(doc, root);
            const char *json_str = yyjson_mut_write(doc, 0, nullptr);
            std::string result(json_str);
            free((void*)json_str);
            yyjson_mut_doc_free(doc);
            return result;
        }

        Buffer from_json_string(const std::string& json_str) {
            yyjson_doc *doc = yyjson_read(json_str.c_str(), json_str.size(), 0);
            if (!doc) {
                // TODO: throw exception
                return Buffer();
            }
            yyjson_val *root = yyjson_doc_get_root(doc);
            Buffer buffer;
            from_yyjson_val(root, buffer, 0);
            yyjson_doc_free(doc);
            return buffer;
        }

        void from_yyjson_val(yyjson_val *val, Buffer& buffer, size_t ofs, const char* key) {
            yyjson_type type = yyjson_get_type(val);
            switch (type) {
                case YYJSON_TYPE_NULL:
                    buffer.set_null(ofs, key);
                    break;
                case YYJSON_TYPE_BOOL:
                    buffer.set_bool(ofs, key, yyjson_get_bool(val));
                    break;
                case YYJSON_TYPE_NUM:
                    if (yyjson_is_int(val)) {
                        buffer.set_i64(ofs, key, yyjson_get_int(val));
                    } else {
                        buffer.set_f64(ofs, key, yyjson_get_real(val));
                    }
                    break;
                case YYJSON_TYPE_STR:
                    buffer.set_str(ofs, key, yyjson_get_str(val));
                    break;
                case YYJSON_TYPE_ARR: {
                    size_t new_ofs = buffer.set_arr(ofs, key);
                    from_yyjson_val(val, buffer, new_ofs);
                    break;
                }
                case YYJSON_TYPE_OBJ: {
                    size_t new_ofs = buffer.set_obj(ofs, key);
                    from_yyjson_val(val, buffer, new_ofs);
                    break;
                }
            }
        }
        
        void from_yyjson_val(yyjson_val *val, Buffer& buffer, size_t ofs) {
            yyjson_type type = yyjson_get_type(val);
            switch (type) {
                case YYJSON_TYPE_NULL:
                    // Not applicable for root, handled by key version
                    break;
                case YYJSON_TYPE_BOOL:
                    // Not applicable for root, handled by key version
                    break;
                case YYJSON_TYPE_NUM:
                    // Not applicable for root, handled by key version
                    break;
                case YYJSON_TYPE_STR:
                    // Not applicable for root, handled by key version
                    break;
                case YYJSON_TYPE_ARR: {
                    buffer.init_array();
                    yyjson_arr_iter iter;
                    yyjson_arr_iter_init(val, &iter);
                    yyjson_val *item;
                    while ((item = yyjson_arr_iter_next(&iter))) {
                        yyjson_type item_type = yyjson_get_type(item);
                        switch (item_type) {
                            case YYJSON_TYPE_NULL:
                                buffer.arr_append_null(ofs);
                                break;
                            case YYJSON_TYPE_BOOL:
                                buffer.arr_append_bool(ofs, yyjson_get_bool(item));
                                break;
                            case YYJSON_TYPE_NUM:
                                if (yyjson_is_int(item)) {
                                    buffer.arr_append_i64(ofs, yyjson_get_int(item));
                                } else {
                                    buffer.arr_append_f64(ofs, yyjson_get_real(item));
                                }
                                break;
                            case YYJSON_TYPE_STR: { // Corrected: Using YYJSON_TYPE_STR
                                const char* str_data = yyjson_get_str(item);
                                size_t str_len = yyjson_get_len(item);
                                buffer.arr_append_str(ofs, std::string_view(str_data, str_len));
                                break;
                            }
                            // yyjson doesn't have a distinct "bytes" type, it treats them as strings.
                            // So if we expect bytes, we handle it as a string from yyjson.
                            // We will assume that if Type::Bytes is used in lite3++, it corresponds to YYJSON_TYPE_STR
                            // which is then hex encoded on serialization.
                            // On deserialization, we are currently not doing hex decoding back to bytes.
                            // This part might need further refinement based on actual requirements.
                            case YYJSON_TYPE_OBJ: { // Corrected: Using YYJSON_TYPE_OBJ
                                size_t new_ofs = buffer.arr_append_obj(ofs);
                                from_yyjson_val(item, buffer, new_ofs);
                                break;
                            }
                            case YYJSON_TYPE_ARR: { // Corrected: Using YYJSON_TYPE_ARR
                                size_t new_ofs = buffer.arr_append_arr(ofs);
                                from_yyjson_val(item, buffer, new_ofs);
                                break;
                            }
                            default:
                                break;
                        }
                    }
                    break;
                }
                case YYJSON_TYPE_OBJ: {
                    buffer.init_object();
                    yyjson_obj_iter iter;
                    yyjson_obj_iter_init(val, &iter);
                    yyjson_val *key, *item;
                    while ((key = yyjson_obj_iter_next(&iter))) {
                        item = yyjson_obj_iter_get_val(key);
                        const char* key_str = yyjson_get_str(key);
                        from_yyjson_val(item, buffer, ofs, key_str);
                    }
                    break;
                }
            }
        }

        yyjson_mut_val* to_yyjson_val(const Buffer& buffer, size_t ofs, yyjson_mut_doc *doc) {
            Type type = static_cast<Type>(buffer.m_data[ofs]);
            switch (type) {
                case Type::Null:
                    return yyjson_mut_null(doc);
                case Type::Bool:
                    return yyjson_mut_bool(doc, *reinterpret_cast<const bool*>(&buffer.m_data[ofs + 1]));
                case Type::Int64:
                    return yyjson_mut_int(doc, *reinterpret_cast<const int64_t*>(&buffer.m_data[ofs + 1]));
                case Type::Float64:
                    return yyjson_mut_real(doc, *reinterpret_cast<const double*>(&buffer.m_data[ofs + 1]));
                case Type::String: {
                    uint32_t size;
                    std::memcpy(&size, &buffer.m_data[ofs + 1], sizeof(size));
                    const char* str_data = reinterpret_cast<const char*>(&buffer.m_data[ofs + 1 + sizeof(size)]);
                    return yyjson_mut_strncpy(doc, str_data, size); // Use strncpy to copy the string
                }
                case Type::Bytes: {
                    uint32_t size;
                    std::memcpy(&size, &buffer.m_data[ofs + 1], sizeof(size));
                    const std::byte* bytes = &buffer.m_data[ofs + 1 + sizeof(size)];
                    std::string hex_str;
                    for (uint32_t i = 0; i < size; ++i) {
                        char buf[3];
                        snprintf(buf, sizeof(buf), "%02x", static_cast<unsigned char>(bytes[i]));
                        hex_str += buf;
                    }
                    return yyjson_mut_str(doc, hex_str.c_str());
                }
                case Type::Object: {
                    yyjson_mut_val *obj = yyjson_mut_obj(doc);
                    for (auto it = buffer.begin(ofs); it != buffer.end(ofs); ++it) {
                        yyjson_mut_obj_add_val(doc, obj, it->key.data(), to_yyjson_val(buffer, it->value_offset, doc));
                    }
                    return obj;
                }
                case Type::Array: {
                    yyjson_mut_val *arr = yyjson_mut_arr(doc);
                    Node node;
                    node.read(buffer, ofs);
                    for (uint32_t i = 0; i < node.size; ++i) {
                        Type value_type = buffer.arr_get_type(ofs, i);
                        switch (value_type) {
                            case Type::Null:
                                yyjson_mut_arr_add_null(doc, arr);
                                break;
                            case Type::Bool:
                                yyjson_mut_arr_add_bool(doc, arr, buffer.arr_get_bool(ofs, i));
                                break;
                            case Type::Int64:
                                yyjson_mut_arr_add_int(doc, arr, buffer.arr_get_i64(ofs, i));
                                break;
                            case Type::Float64:
                                yyjson_mut_arr_add_real(doc, arr, buffer.arr_get_f64(ofs, i));
                                break;
                            case Type::String: {
                                auto str = buffer.arr_get_str(ofs, i);
                                yyjson_mut_arr_add_strncpy(doc, arr, str.data(), str.size());
                                break;
                            }
                            case Type::Bytes: {
                                auto bytes_span = buffer.arr_get_bytes(ofs, i);
                                std::string hex_str;
                                for (size_t j = 0; j < bytes_span.size(); ++j) {
                                    char buf[3];
                                    snprintf(buf, sizeof(buf), "%02x", static_cast<unsigned char>(bytes_span[j]));
                                    hex_str += buf;
                                }
                                yyjson_mut_arr_add_str(doc, arr, hex_str.c_str());
                                break;
                            }
                            case Type::Object:
                                yyjson_mut_arr_add_val(arr, to_yyjson_val(buffer, buffer.arr_get_obj(ofs, i), doc));
                                break;
                            case Type::Array:
                                yyjson_mut_arr_add_val(arr, to_yyjson_val(buffer, buffer.arr_get_arr(ofs, i), doc));
                                break;
                            default:
                                break;
                        }
                    }
                    return arr;
                }
                default:
                    return yyjson_mut_null(doc);
            }
        }
    }
} // namespace lite3