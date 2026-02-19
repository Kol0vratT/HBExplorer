#pragma once

namespace UExplorer
{
    struct FieldReferencePreview
    {
        bool loaded = false;
        bool readFailed = false;
        uintptr_t pointerValue = 0;
    };

    struct SceneEntry
    {
        Unity::Scene scene{};
        std::string label;
    };

    struct ObjectEntry
    {
        Unity::CGameObject* gameObject = nullptr;
        Unity::CTransform* transform = nullptr;
        Unity::CTransform* parent = nullptr;
        int sceneHandle = 0;
        std::string name;
        std::string nameLower;
        std::vector<Unity::CTransform*> children;
    };

    struct ExplorerState
    {
        bool initialized = false;
        bool waitingLogPrinted = false;
        bool methodResolvePrinted = false;

        bool autoRefresh = false;
        bool includeInactive = true;

        ULONGLONG refreshIntervalMs = 2500;
        ULONGLONG lastObjectRefreshTick = 0;
        ULONGLONG lastSceneRefreshTick = 0;

        size_t lastObjectCountLogged = 0;
        int lastSceneCountLogged = 0;

        std::vector<SceneEntry> scenes;
        std::vector<ObjectEntry> objects;

        std::unordered_map<Unity::CTransform*, size_t> indexByTransform;
        std::vector<Unity::CTransform*> rootTransforms;
        std::unordered_set<Unity::CGameObject*> aliveObjects;

        std::unordered_map<Unity::CGameObject*, std::string> classBlobCacheLower;

        Unity::CGameObject* selectedObject = nullptr;
        int selectedSceneHandle = 0; // 0 = all scenes
        std::vector<Unity::CGameObject*> navigationHistory;

        Unity::CGameObject* transformEditTarget = nullptr;
        float editLocalPosition[3]{};
        float editEuler[3]{};
        float editLocalScale[3]{ 1.0f, 1.0f, 1.0f };

        char hierarchyFilter[128]{};
        char classFilter[128]{};
        char nameFilter[128]{};
        char sceneToLoad[128]{};

        void* fnObjectGetInstanceId = nullptr;

        std::unordered_map<uint64_t, std::string> fieldValueDrafts;
        std::unordered_map<uint64_t, FieldReferencePreview> fieldReferencePreviews;
        std::unordered_map<uint64_t, std::string> methodInvokeResults;
        std::unordered_map<uint64_t, std::vector<std::string>> methodArgDrafts;

        void* fnRuntimeInvoke = nullptr;

        bool logAutoScroll = true;
        char logFilter[128]{};

        bool pendingRefresh = false;
    };

    static ExplorerState& GetState()
    {
        static ExplorerState s_State;
        return s_State;
    }

    static std::string SanitizeMemberName(const char* rawName);
    static bool SafeCopyAsciiLabel(const char* source, char* destination, size_t destinationSize);
    static bool SafeReadClassMetadata(
        Unity::il2cppClass* klass,
        const char** outName,
        const char** outNamespace,
        Unity::il2cppClass** outParent);

    static std::string TrimNullTerminator(std::string value)
    {
        const size_t nullPos = value.find('\0');
        if (nullPos != std::string::npos)
            value.resize(nullPos);

        return value;
    }

    static std::string ToLowerCopy(std::string value)
    {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c)
            {
                return static_cast<char>(std::tolower(c));
            });

        return value;
    }

    static std::string UnityStringToUtf8(Unity::System_String* str)
    {
        if (!str)
            return {};

        return TrimNullTerminator(str->ToString());
    }

    static std::string GetClassDisplayName(Unity::il2cppClass* klass)
    {
        if (!klass)
            return "<unknown>";

        const char* rawName = nullptr;
        const char* rawNamespace = nullptr;
        Unity::il2cppClass* unusedParent = nullptr;
        if (!SafeReadClassMetadata(klass, &rawName, &rawNamespace, &unusedParent))
            return "<invalid-class>";

        std::string className = SanitizeMemberName(rawName);
        if (className == "<invalid-name>" || className == "<null>")
            className = "<unnamed>";

        std::string namespaceName = SanitizeMemberName(rawNamespace);
        if (namespaceName == "<invalid-name>" || namespaceName == "<null>")
            namespaceName.clear();

        if (!namespaceName.empty())
            return namespaceName + "." + className;

        return className;
    }

    static std::string GetObjectName(Unity::CObject* obj)
    {
        if (!obj)
            return "<null>";

        std::string result = UnityStringToUtf8(obj->GetName());
        if (result.empty())
            result = "<unnamed>";

        return result;
    }

    static Unity::System_String* SafeGetNameRaw(Unity::CObject* obj)
    {
        if (!obj)
            return nullptr;

        __try
        {
            return obj->CallMethodSafe<Unity::System_String*>("get_name");
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            HBLog::Printf("[UExplorer] EXCEPTION: Object::GetName failed for %p.\n", obj);
            return nullptr;
        }
    }

    static std::string SafeGetObjectName(Unity::CObject* obj)
    {
        if (!obj)
            return "<null>";

        Unity::System_String* rawName = SafeGetNameRaw(obj);
        if (!rawName)
            return "<name-error>";

        std::string result = UnityStringToUtf8(rawName);
        if (result.empty())
            result = "<unnamed>";

        return result;
    }

    static std::string SanitizeMemberName(const char* rawName)
    {
        if (!rawName)
            return "<null>";

        char buffer[128]{};
        if (!SafeCopyAsciiLabel(rawName, buffer, sizeof(buffer)))
            return "<invalid-name>";

        return std::string(buffer);
    }

    static bool IsStaticField(const Unity::il2cppFieldInfo* field)
    {
        if (!field)
            return false;

        return field->m_iOffset < 0;
    }

    static bool IsStaticMethod(const Unity::il2cppMethodInfo* method)
    {
        if (!method)
            return false;

        return (method->m_uFlags & 0x0010U) != 0;
    }

    static constexpr uint32_t kMaxMethodArgsUi = 16U;
    static constexpr uint32_t kMaxMethodArgsInvoke = 16U;
    static constexpr uint32_t kHardMethodArgsLimit = 128U;
    static constexpr size_t kMaxMethodSignatureChars = 384U;
    static constexpr size_t kMaxUiLabelChars = 96U;

    static std::string BuildFallbackMemberName(const char* prefix, const void* identity)
    {
        char buffer[64]{};
        std::snprintf(buffer, sizeof(buffer), "%s_%p", prefix ? prefix : "member", identity);
        return std::string(buffer);
    }

    static std::string MakeSafeMemberLabel(const char* rawName, const char* fallbackPrefix, const void* identity)
    {
        std::string label = SanitizeMemberName(rawName);
        if (label == "<invalid-name>" || label == "<null>" || label.empty())
            return BuildFallbackMemberName(fallbackPrefix, identity);

        return label;
    }

    static std::string ClampUiLabel(const std::string& value, size_t maxChars)
    {
        if (value.size() <= maxChars)
            return value;

        if (maxChars <= 3)
            return value.substr(0, maxChars);

        return value.substr(0, maxChars - 3) + "...";
    }

    static uint64_t BuildFieldKey(const void* owner, const void* field)
    {
        return (static_cast<uint64_t>(reinterpret_cast<uintptr_t>(owner)) << 1ULL)
            ^ static_cast<uint64_t>(reinterpret_cast<uintptr_t>(field));
    }

    static uint64_t BuildMethodKey(const void* owner, const void* method)
    {
        return (static_cast<uint64_t>(reinterpret_cast<uintptr_t>(owner)) << 1ULL)
            ^ (static_cast<uint64_t>(reinterpret_cast<uintptr_t>(method)) << 3ULL);
    }

    enum Il2CppTypeCode : unsigned int
    {
        TypeCode_Void = 1,
        TypeCode_Boolean = 2,
        TypeCode_Char = 3,
        TypeCode_I1 = 4,
        TypeCode_U1 = 5,
        TypeCode_I2 = 6,
        TypeCode_U2 = 7,
        TypeCode_I4 = 8,
        TypeCode_U4 = 9,
        TypeCode_R4 = 12,
        TypeCode_R8 = 13,
        TypeCode_String = 14,
        TypeCode_Class = 18,
        TypeCode_Object = 28,
    };

    enum class EditableValueType : int
    {
        Unsupported = 0,
        Bool,
        Int,
        Float,
        Double,
        Short,
        Byte,
        Char,
        String,
    };

    static const ImVec4 kColorMethod = ImVec4(1.00f, 0.58f, 0.10f, 1.00f);
    static const ImVec4 kColorField = ImVec4(0.72f, 0.42f, 0.96f, 1.00f);
    static const ImVec4 kColorClass = ImVec4(0.32f, 0.86f, 0.92f, 1.00f);

    struct AnimatedButtonState
    {
        float hoverT = 0.0f;
        float activeT = 0.0f;
        int lastFrameSeen = 0;
    };

    static ImVec4 LerpColor(const ImVec4& a, const ImVec4& b, float t)
    {
        return ImVec4(
            a.x + (b.x - a.x) * t,
            a.y + (b.y - a.y) * t,
            a.z + (b.z - a.z) * t,
            a.w + (b.w - a.w) * t);
    }

    static float Clamp01(float value)
    {
        if (value < 0.0f) return 0.0f;
        if (value > 1.0f) return 1.0f;
        return value;
    }

    static ImVec4 RaiseColor(const ImVec4& c, float amount)
    {
        return ImVec4(
            Clamp01(c.x + amount),
            Clamp01(c.y + amount),
            Clamp01(c.z + amount),
            c.w);
    }

    static bool AnimatedButton(const char* label, const ImVec2& size = ImVec2(0.0f, 0.0f))
    {
        if (!label)
            return false;

        static std::unordered_map<ImGuiID, AnimatedButtonState> s_ButtonStates;

        const ImGuiID id = ImGui::GetID(label);
        AnimatedButtonState& anim = s_ButtonStates[id];

        const ImGuiStyle& style = ImGui::GetStyle();
        const ImVec4 base = style.Colors[ImGuiCol_Button];
        const ImVec4 hoveredBase = style.Colors[ImGuiCol_ButtonHovered];
        const ImVec4 activeBase = style.Colors[ImGuiCol_ButtonActive];

        ImVec4 buttonColor = LerpColor(base, hoveredBase, anim.hoverT * 0.70f);
        buttonColor = LerpColor(buttonColor, activeBase, anim.activeT * 0.75f);

        const ImVec4 buttonHoveredColor = LerpColor(hoveredBase, RaiseColor(hoveredBase, 0.06f), anim.hoverT * 0.65f);
        const ImVec4 buttonActiveColor = LerpColor(activeBase, RaiseColor(activeBase, 0.08f), anim.activeT * 0.65f);

        ImGui::PushStyleColor(ImGuiCol_Button, buttonColor);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, buttonHoveredColor);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, buttonActiveColor);
        const bool pressed = ImGui::Button(label, size);
        const bool isHovered = ImGui::IsItemHovered();
        const bool isActive = ImGui::IsItemActive();
        ImGui::PopStyleColor(3);

        const float dt = ImGui::GetIO().DeltaTime;
        const float hoverSpeed = 10.0f;
        const float activeSpeed = 18.0f;
        anim.hoverT = Clamp01(anim.hoverT + (isHovered ? dt * hoverSpeed : -dt * hoverSpeed));
        anim.activeT = Clamp01(anim.activeT + (isActive ? dt * activeSpeed : -dt * activeSpeed));
        anim.lastFrameSeen = ImGui::GetFrameCount();

        const float pulse = (anim.hoverT * 0.30f) + (anim.activeT * 0.65f);
        if (pulse > 0.001f)
        {
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            const ImVec2 rectMin = ImGui::GetItemRectMin();
            const ImVec2 rectMax = ImGui::GetItemRectMax();
            const float rounding = style.FrameRounding;
            const ImVec4 glow = ImVec4(0.88f, 0.88f, 0.90f, 0.20f * pulse);
            drawList->AddRect(rectMin, rectMax, ImGui::GetColorU32(glow), rounding, 0, 1.0f);
        }

        if ((ImGui::GetFrameCount() % 300) == 0 && s_ButtonStates.size() > 2048)
        {
            const int keepAfterFrame = ImGui::GetFrameCount() - 900;
            for (auto it = s_ButtonStates.begin(); it != s_ButtonStates.end();)
            {
                if (it->second.lastFrameSeen < keepAfterFrame)
                    it = s_ButtonStates.erase(it);
                else
                    ++it;
            }
        }

        return pressed;
    }

    static EditableValueType MapEditableType(unsigned int typeCode)
    {
        switch (typeCode)
        {
        case TypeCode_Boolean:
            return EditableValueType::Bool;
        case TypeCode_String:
            return EditableValueType::String;
        case TypeCode_R4:
            return EditableValueType::Float;
        case TypeCode_R8:
            return EditableValueType::Double;
        case TypeCode_I4:
        case TypeCode_U4:
            return EditableValueType::Int;
        case TypeCode_I2:
        case TypeCode_U2:
            return EditableValueType::Short;
        case TypeCode_I1:
        case TypeCode_U1:
            return EditableValueType::Byte;
        case TypeCode_Char:
            return EditableValueType::Char;
        default:
            return EditableValueType::Unsupported;
        }
    }

    static std::string TrimAsciiCopy(const std::string& value)
    {
        size_t begin = 0;
        size_t end = value.size();

        while (begin < end && std::isspace(static_cast<unsigned char>(value[begin])))
            ++begin;
        while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1])))
            --end;

        return value.substr(begin, end - begin);
    }

    template<typename TInt>
    static bool ParseSignedIntegerText(const std::string& text, TInt* outValue)
    {
        if (!outValue)
            return false;

        const std::string trimmed = TrimAsciiCopy(text);
        if (trimmed.empty())
            return false;

        char* end = nullptr;
        const long long value = std::strtoll(trimmed.c_str(), &end, 10);
        if (!end || *end != '\0')
            return false;

        if (value < static_cast<long long>(std::numeric_limits<TInt>::min()) ||
            value > static_cast<long long>(std::numeric_limits<TInt>::max()))
        {
            return false;
        }

        *outValue = static_cast<TInt>(value);
        return true;
    }

    template<typename TInt>
    static bool ParseUnsignedIntegerText(const std::string& text, TInt* outValue)
    {
        if (!outValue)
            return false;

        const std::string trimmed = TrimAsciiCopy(text);
        if (trimmed.empty())
            return false;

        if (!trimmed.empty() && trimmed[0] == '-')
            return false;

        char* end = nullptr;
        const unsigned long long value = std::strtoull(trimmed.c_str(), &end, 10);
        if (!end || *end != '\0')
            return false;

        if (value > static_cast<unsigned long long>(std::numeric_limits<TInt>::max()))
            return false;

        *outValue = static_cast<TInt>(value);
        return true;
    }

    static bool ParseFloatText(const std::string& text, float* outValue)
    {
        if (!outValue)
            return false;

        const std::string trimmed = TrimAsciiCopy(text);
        if (trimmed.empty())
            return false;

        char* end = nullptr;
        const float value = std::strtof(trimmed.c_str(), &end);
        if (!end || *end != '\0')
            return false;

        *outValue = value;
        return true;
    }

    static bool ParseDoubleText(const std::string& text, double* outValue)
    {
        if (!outValue)
            return false;

        const std::string trimmed = TrimAsciiCopy(text);
        if (trimmed.empty())
            return false;

        char* end = nullptr;
        const double value = std::strtod(trimmed.c_str(), &end);
        if (!end || *end != '\0')
            return false;

        *outValue = value;
        return true;
    }

    static bool ParseCharText(const std::string& text, uint16_t* outValue)
    {
        if (!outValue)
            return false;

        const std::string trimmed = TrimAsciiCopy(text);
        if (trimmed.empty())
            return false;

        if (trimmed.size() == 1)
        {
            *outValue = static_cast<uint16_t>(static_cast<unsigned char>(trimmed[0]));
            return true;
        }

        unsigned int code = 0;
        if (!ParseUnsignedIntegerText<unsigned int>(trimmed, &code) || code > 0xFFFFU)
            return false;

        *outValue = static_cast<uint16_t>(code);
        return true;
    }

    static bool ParseBoolText(const std::string& text, bool* outValue)
    {
        if (!outValue)
            return false;

        const std::string trimmed = ToLowerCopy(TrimAsciiCopy(text));
        if (trimmed.empty())
            return false;

        if (trimmed == "1" || trimmed == "true" || trimmed == "yes" || trimmed == "on")
        {
            *outValue = true;
            return true;
        }

        if (trimmed == "0" || trimmed == "false" || trimmed == "no" || trimmed == "off")
        {
            *outValue = false;
            return true;
        }

        return false;
    }

    static unsigned int MapSystemClassToTypeCode(Unity::il2cppClass* typeClass)
    {
        if (!typeClass)
            return 0U;

        const char* ns = typeClass->m_pNamespace ? typeClass->m_pNamespace : "";
        const char* name = typeClass->m_pName ? typeClass->m_pName : "";

        if (std::strcmp(ns, "System") != 0)
            return 0U;

        if (std::strcmp(name, "Boolean") == 0) return TypeCode_Boolean;
        if (std::strcmp(name, "Char") == 0) return TypeCode_Char;
        if (std::strcmp(name, "SByte") == 0) return TypeCode_I1;
        if (std::strcmp(name, "Byte") == 0) return TypeCode_U1;
        if (std::strcmp(name, "Int16") == 0) return TypeCode_I2;
        if (std::strcmp(name, "UInt16") == 0) return TypeCode_U2;
        if (std::strcmp(name, "Int32") == 0) return TypeCode_I4;
        if (std::strcmp(name, "UInt32") == 0) return TypeCode_U4;
        if (std::strcmp(name, "Single") == 0) return TypeCode_R4;
        if (std::strcmp(name, "Double") == 0) return TypeCode_R8;
        if (std::strcmp(name, "String") == 0) return TypeCode_String;
        if (std::strcmp(name, "Object") == 0) return TypeCode_Object;
        return 0U;
    }

    static unsigned int GetFieldTypeEnum(Unity::il2cppType* type)
    {
        if (!type)
            return 0U;

#ifdef UNITY_VERSION_2022_3_8F1
        unsigned int typeCode = (type->bits >> 16) & 0xFFU;
        if (typeCode == 0U)
            typeCode = type->bits & 0xFFU;
#else
        unsigned int typeCode = type->m_uType;
#endif

        if (typeCode == Unity::Type_ValueType ||
            typeCode == Unity::Type_Class ||
            typeCode == Unity::Type_Enum ||
            typeCode == TypeCode_Object)
        {
            Unity::il2cppClass* typeClass = IL2CPP::Class::Utils::ClassFromType(type);
            const unsigned int mappedPrimitive = MapSystemClassToTypeCode(typeClass);
            if (mappedPrimitive != 0U)
                return mappedPrimitive;
        }

        return typeCode;
    }

    static std::string GetFieldTypeName(Unity::il2cppType* type)
    {
        if (!type)
            return "unknown";

        const unsigned int typeEnum = GetFieldTypeEnum(type);
        switch (typeEnum)
        {
        case TypeCode_Boolean: return "bool";
        case TypeCode_Char: return "char";
        case TypeCode_I1: return "sbyte";
        case TypeCode_U1: return "byte";
        case TypeCode_I2: return "short";
        case TypeCode_U2: return "ushort";
        case TypeCode_I4: return "int";
        case TypeCode_U4: return "uint";
        case TypeCode_R4: return "float";
        case TypeCode_R8: return "double";
        case TypeCode_String: return "string";
        case Unity::Type_Array: return "array";
        case Unity::Type_Pointer: return "pointer";
        default:
            break;
        }

        if (Unity::il2cppClass* typeClass = IL2CPP::Class::Utils::ClassFromType(type))
            return GetClassDisplayName(typeClass);

        switch (typeEnum)
        {
        case TypeCode_Class: return "class";
        case TypeCode_Object: return "object";
        default: return "unknown";
        }
    }

    static void ResolveRuntimeMethods(ExplorerState& state)
    {
        if (!state.fnObjectGetInstanceId)
        {
            state.fnObjectGetInstanceId = IL2CPP::ResolveUnityMethod(UNITY_OBJECT_CLASS, "GetInstanceID", 0);
            if (!state.fnObjectGetInstanceId)
            {
                state.fnObjectGetInstanceId = IL2CPP::ResolveCallAny(
                    {
                        IL2CPP_RStr(UNITY_OBJECT_CLASS "::GetInstanceID"),
                    });
            }
        }

        if (!state.fnRuntimeInvoke && IL2CPP::Globals.m_GameAssembly)
        {
            state.fnRuntimeInvoke = GetProcAddress(IL2CPP::Globals.m_GameAssembly, "il2cpp_runtime_invoke");
        }

        if (!state.methodResolvePrinted)
        {
            HBLog::Printf("[UExplorer] Method resolve: Object.GetInstanceID=%p il2cpp_runtime_invoke=%p\n",
                state.fnObjectGetInstanceId,
                state.fnRuntimeInvoke);
            state.methodResolvePrinted = true;
        }
    }

    static size_t SafeGetModuleImageSize(HMODULE module)
    {
        if (!module)
            return 0;

        __try
        {
            const IMAGE_DOS_HEADER* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
            if (dos->e_magic != IMAGE_DOS_SIGNATURE)
                return 0;

            const IMAGE_NT_HEADERS* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(
                reinterpret_cast<uintptr_t>(module) + static_cast<uintptr_t>(dos->e_lfanew));
            if (nt->Signature != IMAGE_NT_SIGNATURE)
                return 0;

            return static_cast<size_t>(nt->OptionalHeader.SizeOfImage);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    static bool TryGetGameAssemblyRva(void* address, uintptr_t* outRva)
    {
        if (outRva)
            *outRva = 0;

        if (!address || !outRva || !IL2CPP::Globals.m_GameAssembly)
            return false;

        const uintptr_t moduleBase = reinterpret_cast<uintptr_t>(IL2CPP::Globals.m_GameAssembly);
        const uintptr_t target = reinterpret_cast<uintptr_t>(address);
        if (target < moduleBase)
            return false;

        const uintptr_t rva = target - moduleBase;
        const size_t imageSize = SafeGetModuleImageSize(IL2CPP::Globals.m_GameAssembly);
        if (imageSize != 0 && rva >= imageSize)
            return false;

        *outRva = rva;
        return true;
    }

    static bool SafeReadMethodMetadata(
        Unity::il2cppMethodInfo* method,
        const char** outName,
        Unity::il2cppType** outReturnType,
        void** outMethodPointer,
        void** outInvokerPointer,
        uint32_t* outArgCount,
        uint32_t* outFlags)
    {
        if (outName) *outName = nullptr;
        if (outReturnType) *outReturnType = nullptr;
        if (outMethodPointer) *outMethodPointer = nullptr;
        if (outInvokerPointer) *outInvokerPointer = nullptr;
        if (outArgCount) *outArgCount = 0;
        if (outFlags) *outFlags = 0;

        if (!method)
            return false;

        __try
        {
            if (outName) *outName = method->m_pName;
            if (outReturnType) *outReturnType = method->m_pReturnType;
            if (outMethodPointer) *outMethodPointer = method->m_pMethodPointer;
            if (outInvokerPointer) *outInvokerPointer = method->m_pInvokerMethod;
            if (outArgCount) *outArgCount = static_cast<uint32_t>(method->m_uArgsCount);
            if (outFlags) *outFlags = static_cast<uint32_t>(method->m_uFlags);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    static Unity::il2cppType* SafeGetMethodParamType(Unity::il2cppMethodInfo* method, uint32_t index)
    {
        if (!method)
            return nullptr;

        __try
        {
            return IL2CPP::Class::Utils::GetMethodParamType(method, index);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }

    static const char* SafeGetMethodParamName(Unity::il2cppMethodInfo* method, uint32_t index)
    {
        if (!method)
            return nullptr;

        __try
        {
            return IL2CPP::Class::Utils::MethodGetParamName(method, index);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }

    static bool SafeFetchMethods(Unity::CComponent* component, std::vector<Unity::il2cppMethodInfo*>* outMethods)
    {
        if (!outMethods)
            return false;

        outMethods->clear();
        if (!component)
            return false;

        __try
        {
            component->FetchMethods(outMethods);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

#ifdef _WIN64
    using RuntimeInvokeFn = Unity::il2cppObject * (__fastcall*)(void*, void*, void**, void**);
#else
    using RuntimeInvokeFn = Unity::il2cppObject * (__cdecl*)(void*, void*, void**, void**);
#endif

    static Unity::il2cppObject* SafeRuntimeInvokeCall(
        RuntimeInvokeFn runtimeInvoke,
        void* method,
        void* instance,
        void** args,
        void** exception,
        bool* outFaulted)
    {
        if (outFaulted)
            *outFaulted = false;

        if (!runtimeInvoke)
            return nullptr;

        __try
        {
            return runtimeInvoke(method, instance, args, exception);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            if (outFaulted)
                *outFaulted = true;
            return nullptr;
        }
    }

    static int GetInstanceId(ExplorerState& state, Unity::CObject* object)
    {
        if (!object || !state.fnObjectGetInstanceId)
            return 0;

        return reinterpret_cast<int(UNITY_CALLING_CONVENTION)(void*)>(state.fnObjectGetInstanceId)(object);
    }

    static int GetSceneHandleForGameObject(ExplorerState& state, Unity::CGameObject* gameObject)
    {
        (void)state;
        (void)gameObject;
        return 0;
    }

    static Unity::il2cppArray<Unity::CGameObject*>* SafeFindGameObjects(bool includeInactive)
    {
        __try
        {
            return Unity::Object::FindObjectsOfType<Unity::CGameObject>(UNITY_GAMEOBJECT_CLASS, includeInactive);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            HBLog::Printf("[UExplorer] EXCEPTION: FindObjectsOfType<UnityEngine.GameObject> failed.\n");
            return nullptr;
        }
    }

    static Unity::CTransform* SafeGetTransform(Unity::CGameObject* gameObject)
    {
        if (!gameObject)
            return nullptr;

        __try
        {
            return gameObject->GetTransform();
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            HBLog::Printf("[UExplorer] EXCEPTION: GameObject::GetTransform failed for %p.\n", gameObject);
            return nullptr;
        }
    }

    static Unity::CTransform* SafeGetParent(Unity::CTransform* transform)
    {
        if (!transform)
            return nullptr;

        __try
        {
            return transform->GetParent();
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            HBLog::Printf("[UExplorer] EXCEPTION: Transform::GetParent failed for %p.\n", transform);
            return nullptr;
        }
    }

    static Unity::il2cppArray<Unity::CComponent*>* SafeGetComponents(Unity::CGameObject* gameObject, Unity::il2cppObject* componentType)
    {
        if (!gameObject || !componentType)
            return nullptr;

        __try
        {
            return gameObject->GetComponents(componentType);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            HBLog::Printf("[UExplorer] EXCEPTION: GameObject::GetComponents failed for %p.\n", gameObject);
            return nullptr;
        }
    }

    static Unity::CGameObject* SafeArrayGetGameObject(Unity::il2cppArray<Unity::CGameObject*>* array, unsigned int index)
    {
        if (!array)
            return nullptr;

        __try
        {
            return array->operator[](index);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            HBLog::Printf("[UExplorer] EXCEPTION: GameObject array access failed at index %u.\n", index);
            return nullptr;
        }
    }

    static bool SafeReadObjectClass(Unity::il2cppObject* object, Unity::il2cppClass** outClass)
    {
        if (outClass)
            *outClass = nullptr;

        if (!object || !outClass)
            return false;

        __try
        {
            *outClass = object->m_pClass;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            HBLog::Printf("[UExplorer] EXCEPTION: reading object class failed for %p.\n", object);
            return false;
        }
    }

    static Unity::CComponent* SafeArrayGetComponent(Unity::il2cppArray<Unity::CComponent*>* array, unsigned int index)
    {
        if (!array)
            return nullptr;

        __try
        {
            return array->operator[](index);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }

    static bool SafeReadUnityObjectCachedPtr(Unity::il2cppObject* object, void** outCachedPtr)
    {
        if (outCachedPtr)
            *outCachedPtr = nullptr;

        if (!object || !outCachedPtr)
            return false;

        __try
        {
            *outCachedPtr = *reinterpret_cast<void**>(
                reinterpret_cast<uintptr_t>(object) + sizeof(Unity::il2cppObject));
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    static bool SafeGetObjectInstanceId(ExplorerState& state, Unity::il2cppObject* object, int* outInstanceId)
    {
        if (outInstanceId)
            *outInstanceId = 0;

        if (!object || !outInstanceId || !state.fnObjectGetInstanceId)
            return false;

        __try
        {
            *outInstanceId = reinterpret_cast<int(UNITY_CALLING_CONVENTION)(void*)>(
                state.fnObjectGetInstanceId)(object);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    static bool SafeReadClassMetadata(
        Unity::il2cppClass* klass,
        const char** outName,
        const char** outNamespace,
        Unity::il2cppClass** outParent)
    {
        if (outName) *outName = nullptr;
        if (outNamespace) *outNamespace = nullptr;
        if (outParent) *outParent = nullptr;

        if (!klass)
            return false;

        __try
        {
            if (outName) *outName = klass->m_pName;
            if (outNamespace) *outNamespace = klass->m_pNamespace;
            if (outParent) *outParent = klass->m_pParentClass;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    static bool SafeCopyAsciiLabel(const char* source, char* destination, size_t destinationSize)
    {
        if (!destination || destinationSize == 0)
            return false;

        destination[0] = '\0';
        if (!source)
            return false;

        __try
        {
            size_t i = 0;
            for (; i + 1 < destinationSize; ++i)
            {
                const unsigned char c = static_cast<unsigned char>(source[i]);
                if (c == '\0')
                {
                    destination[i] = '\0';
                    return (i > 0);
                }

                if (c < 0x20 || c > 0x7E)
                    return false;

                destination[i] = static_cast<char>(c);
            }

            destination[destinationSize - 1] = '\0';
            return false;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            destination[0] = '\0';
            return false;
        }
    }

    static bool SafeAsciiEquals(const char* text, const char* expected, size_t maxLen = 128)
    {
        if (!text || !expected || maxLen == 0)
            return false;

        __try
        {
            size_t i = 0;
            for (; i < maxLen; ++i)
            {
                const char e = expected[i];
                const char t = text[i];

                if (e == '\0')
                    return t == '\0';
                if (t == '\0')
                    return false;
                if (t != e)
                    return false;
            }
            return false;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    static std::string SafeGetClassDisplayName(Unity::il2cppClass* klass)
    {
        const char* rawName = nullptr;
        const char* rawNamespace = nullptr;
        Unity::il2cppClass* unusedParent = nullptr;
        if (!SafeReadClassMetadata(klass, &rawName, &rawNamespace, &unusedParent))
            return "<invalid-class>";

        char nameBuffer[96]{};
        char namespaceBuffer[96]{};

        const bool hasName = SafeCopyAsciiLabel(rawName, nameBuffer, sizeof(nameBuffer));
        const bool hasNamespace = SafeCopyAsciiLabel(rawNamespace, namespaceBuffer, sizeof(namespaceBuffer));

        if (!hasName)
            return "<unnamed-class>";

        if (hasNamespace && namespaceBuffer[0] != '\0')
            return std::string(namespaceBuffer) + "." + nameBuffer;

        return std::string(nameBuffer);
    }

    static bool IsClassOrParent(Unity::il2cppClass* klass, const char* namespaceName, const char* className)
    {
        if (!klass || !className)
            return false;

        Unity::il2cppClass* current = klass;
        for (int depth = 0; depth < 64 && current; ++depth)
        {
            const char* currentName = nullptr;
            const char* currentNamespace = nullptr;
            Unity::il2cppClass* parent = nullptr;
            if (!SafeReadClassMetadata(current, &currentName, &currentNamespace, &parent))
                return false;

            if (SafeAsciiEquals(currentName, className))
            {
                if (!namespaceName || namespaceName[0] == '\0')
                    return true;

                if (SafeAsciiEquals(currentNamespace, namespaceName))
                    return true;
            }

            if (parent == current)
                break;
            current = parent;
        }

        return false;
    }

    static Unity::CGameObject* ResolveInspectableGameObjectFromCache(ExplorerState& state, Unity::il2cppObject* object)
    {
        if (!object)
            return nullptr;

        Unity::il2cppClass* objectClass = nullptr;
        if (!SafeReadObjectClass(object, &objectClass) || !objectClass)
            return nullptr;

        const bool isUnityObject = IsClassOrParent(objectClass, "UnityEngine", "Object");
        if (!isUnityObject)
            return nullptr;

        const bool isGameObject = IsClassOrParent(objectClass, "UnityEngine", "GameObject");
        const bool isComponent = IsClassOrParent(objectClass, "UnityEngine", "Component");

        void* targetCachedPtr = nullptr;
        const bool hasTargetCachedPtr = SafeReadUnityObjectCachedPtr(object, &targetCachedPtr) && (targetCachedPtr != nullptr);

        int targetInstanceId = 0;
        const bool hasTargetInstanceId = SafeGetObjectInstanceId(state, object, &targetInstanceId) && (targetInstanceId != 0);

        Unity::CGameObject* asGameObject = reinterpret_cast<Unity::CGameObject*>(object);
        if (isGameObject && state.aliveObjects.find(asGameObject) != state.aliveObjects.end())
            return asGameObject;

        if (isGameObject)
        {
            for (const ObjectEntry& entry : state.objects)
            {
                Unity::CGameObject* gameObject = entry.gameObject;
                if (!gameObject)
                    continue;

                if (gameObject == asGameObject)
                    return gameObject;

                if (hasTargetCachedPtr && gameObject->m_CachedPtr == targetCachedPtr)
                    return gameObject;

                if (hasTargetInstanceId && state.fnObjectGetInstanceId)
                {
                    const int candidateId = GetInstanceId(state, gameObject);
                    if (candidateId == targetInstanceId)
                        return gameObject;
                }
            }
        }

        if (!isComponent)
            return nullptr;

        Unity::il2cppObject* componentType = IL2CPP::SystemTypeCache::Get(UNITY_COMPONENT_CLASS);
        if (!componentType)
            componentType = IL2CPP::Class::GetSystemType(UNITY_COMPONENT_CLASS);
        if (!componentType)
            return nullptr;

        for (const ObjectEntry& entry : state.objects)
        {
            Unity::CGameObject* gameObject = entry.gameObject;
            if (!gameObject)
                continue;

            Unity::il2cppArray<Unity::CComponent*>* components = SafeGetComponents(gameObject, componentType);
            if (!components)
                continue;

            for (uintptr_t i = 0; i < components->m_uMaxLength; ++i)
            {
                Unity::CComponent* component = SafeArrayGetComponent(components, static_cast<unsigned int>(i));
                if (!component)
                    continue;

                if (reinterpret_cast<Unity::il2cppObject*>(component) == object)
                    return gameObject;

                if (hasTargetCachedPtr && component->m_CachedPtr == targetCachedPtr)
                    return gameObject;

                if (hasTargetInstanceId)
                {
                    int componentInstanceId = 0;
                    if (SafeGetObjectInstanceId(state, reinterpret_cast<Unity::il2cppObject*>(component), &componentInstanceId))
                    {
                        if (componentInstanceId == targetInstanceId)
                            return gameObject;
                    }
                }
            }
        }

        return nullptr;
    }

    static void SelectObjectDirect(ExplorerState& state, Unity::CGameObject* targetObject)
    {
        if (!targetObject)
            return;

        state.navigationHistory.clear();
        state.selectedObject = targetObject;
        state.transformEditTarget = nullptr;
    }

    static void NavigateToReferencedObject(ExplorerState& state, Unity::CGameObject* targetObject)
    {
        if (!targetObject)
            return;

        if (state.selectedObject && state.selectedObject != targetObject)
            state.navigationHistory.emplace_back(state.selectedObject);

        state.selectedObject = targetObject;
        state.transformEditTarget = nullptr;
    }

    static bool NavigateBack(ExplorerState& state)
    {
        while (!state.navigationHistory.empty())
        {
            Unity::CGameObject* previous = state.navigationHistory.back();
            state.navigationHistory.pop_back();
            if (previous && state.aliveObjects.find(previous) != state.aliveObjects.end())
            {
                state.selectedObject = previous;
                state.transformEditTarget = nullptr;
                HBLog::Printf("[UExplorer] Navigate back -> %s\n", SafeGetObjectName(previous).c_str());
                return true;
            }
        }

        return false;
    }

    template<typename T>
    static bool TryGetStaticFieldValue(Unity::il2cppFieldInfo* field, T* outValue)
    {
        if (!field || !outValue || !IL2CPP::Functions.m_FieldStaticGetValue)
            return false;

        __try
        {
            reinterpret_cast<void(IL2CPP_CALLING_CONVENTION)(Unity::il2cppFieldInfo*, void*)>(
                IL2CPP::Functions.m_FieldStaticGetValue)(field, outValue);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    template<typename T>
    static bool TrySetStaticFieldValue(Unity::il2cppFieldInfo* field, T* value)
    {
        if (!field || !value || !IL2CPP::Functions.m_FieldStaticSetValue)
            return false;

        __try
        {
            reinterpret_cast<void(IL2CPP_CALLING_CONVENTION)(Unity::il2cppFieldInfo*, void*)>(
                IL2CPP::Functions.m_FieldStaticSetValue)(field, value);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    struct MethodArgValue
    {
        EditableValueType type = EditableValueType::Unsupported;
        bool boolValue = false;
        int intValue = 0;
        uint32_t uintValue = 0;
        float floatValue = 0.0f;
        double doubleValue = 0.0;
        short shortValue = 0;
        uint16_t ushortValue = 0;
        int8_t sbyteValue = 0;
        unsigned char byteValue = 0;
        uint16_t charValue = 0;
        Unity::System_String* stringValue = nullptr;
    };

    static const char* EditableTypeDisplayName(EditableValueType type)
    {
        switch (type)
        {
        case EditableValueType::Bool: return "bool";
        case EditableValueType::Int: return "int";
        case EditableValueType::Float: return "float";
        case EditableValueType::Double: return "double";
        case EditableValueType::Short: return "short";
        case EditableValueType::Byte: return "byte";
        case EditableValueType::Char: return "char";
        case EditableValueType::String: return "string";
        default: return "unsupported";
        }
    }

    static bool ParseMethodArgument(unsigned int typeCode, const std::string& text, MethodArgValue* outArg)
    {
        if (!outArg)
            return false;

        outArg->type = MapEditableType(typeCode);
        switch (typeCode)
        {
        case TypeCode_Boolean:
            return ParseBoolText(text, &outArg->boolValue);
        case TypeCode_I4:
            return ParseSignedIntegerText<int>(text, &outArg->intValue);
        case TypeCode_U4:
            return ParseUnsignedIntegerText<uint32_t>(text, &outArg->uintValue);
        case TypeCode_R4:
            return ParseFloatText(text, &outArg->floatValue);
        case TypeCode_R8:
            return ParseDoubleText(text, &outArg->doubleValue);
        case TypeCode_I2:
            return ParseSignedIntegerText<short>(text, &outArg->shortValue);
        case TypeCode_U2:
            return ParseUnsignedIntegerText<uint16_t>(text, &outArg->ushortValue);
        case TypeCode_I1:
            return ParseSignedIntegerText<int8_t>(text, &outArg->sbyteValue);
        case TypeCode_U1:
            return ParseUnsignedIntegerText<unsigned char>(text, &outArg->byteValue);
        case TypeCode_Char:
            return ParseCharText(text, &outArg->charValue);
        case TypeCode_String:
            outArg->stringValue = IL2CPP::String::New(text.c_str());
            return outArg->stringValue != nullptr;
        default:
            return false;
        }
    }

    static void* GetMethodArgumentPointer(unsigned int typeCode, MethodArgValue* arg)
    {
        if (!arg)
            return nullptr;

        switch (typeCode)
        {
        case TypeCode_Boolean: return &arg->boolValue;
        case TypeCode_I4: return &arg->intValue;
        case TypeCode_U4: return &arg->uintValue;
        case TypeCode_R4: return &arg->floatValue;
        case TypeCode_R8: return &arg->doubleValue;
        case TypeCode_I2: return &arg->shortValue;
        case TypeCode_U2: return &arg->ushortValue;
        case TypeCode_I1: return &arg->sbyteValue;
        case TypeCode_U1: return &arg->byteValue;
        case TypeCode_Char: return &arg->charValue;
        case TypeCode_String: return arg->stringValue;
        default: return nullptr;
        }
    }

    static std::string BuildMethodSignature(Unity::il2cppMethodInfo* method, uint32_t displayedArgCount, uint32_t totalArgCount)
    {
        if (!method)
            return "<null-method>";

        const char* rawMethodName = nullptr;
        if (!SafeReadMethodMetadata(method, &rawMethodName, nullptr, nullptr, nullptr, nullptr, nullptr))
            return "<invalid-method>";

        std::string signature = MakeSafeMemberLabel(rawMethodName, "method", method);
        signature = ClampUiLabel(signature, kMaxUiLabelChars);
        signature += "(";

        for (uint32_t i = 0; i < displayedArgCount; ++i)
        {
            Unity::il2cppType* paramType = SafeGetMethodParamType(method, i);
            const char* paramNameRaw = SafeGetMethodParamName(method, i);
            std::string paramTypeName = ClampUiLabel(GetFieldTypeName(paramType), kMaxUiLabelChars);

            std::string paramName = paramNameRaw ? SanitizeMemberName(paramNameRaw) : ("arg" + std::to_string(i));
            if (paramName == "<invalid-name>" || paramName == "<null>" || paramName.empty())
                paramName = "arg" + std::to_string(i);

            const std::string part = paramTypeName + " " + paramName;
            if ((signature.size() + part.size() + 8U) >= kMaxMethodSignatureChars)
            {
                signature += "...";
                break;
            }

            signature += part;
            if ((i + 1U) < displayedArgCount)
                signature += ", ";
        }

        if (totalArgCount > displayedArgCount)
        {
            if (displayedArgCount > 0)
                signature += ", ";

            signature += "... +" + std::to_string(totalArgCount - displayedArgCount);
        }

        signature += ")";
        return ClampUiLabel(signature, kMaxMethodSignatureChars);
    }

    static std::string FormatRuntimeInvokeReturn(unsigned int returnType, Unity::il2cppObject* retObject)
    {
        char buffer[128]{};
        if (returnType == TypeCode_Void)
            return "void";

        if (!retObject)
            return "<null>";

        auto readBoxed = [&](auto* dummy)
        {
            using T = std::remove_pointer_t<decltype(dummy)>;
            return *reinterpret_cast<T*>(reinterpret_cast<uintptr_t>(retObject) + sizeof(Unity::il2cppObject));
        };

        switch (returnType)
        {
        case TypeCode_I4:
            std::snprintf(buffer, sizeof(buffer), "%d", readBoxed((int*)nullptr));
            return buffer;
        case TypeCode_U4:
            std::snprintf(buffer, sizeof(buffer), "%u", readBoxed((uint32_t*)nullptr));
            return buffer;
        case TypeCode_R4:
            std::snprintf(buffer, sizeof(buffer), "%.6f", readBoxed((float*)nullptr));
            return buffer;
        case TypeCode_R8:
            std::snprintf(buffer, sizeof(buffer), "%.9f", readBoxed((double*)nullptr));
            return buffer;
        case TypeCode_I2:
            std::snprintf(buffer, sizeof(buffer), "%d", static_cast<int>(readBoxed((short*)nullptr)));
            return buffer;
        case TypeCode_U2:
            std::snprintf(buffer, sizeof(buffer), "%u", static_cast<unsigned int>(readBoxed((uint16_t*)nullptr)));
            return buffer;
        case TypeCode_I1:
            std::snprintf(buffer, sizeof(buffer), "%d", static_cast<int>(readBoxed((int8_t*)nullptr)));
            return buffer;
        case TypeCode_U1:
            std::snprintf(buffer, sizeof(buffer), "%u", static_cast<unsigned int>(readBoxed((unsigned char*)nullptr)));
            return buffer;
        case TypeCode_Char:
        {
            const uint16_t c = readBoxed((uint16_t*)nullptr);
            std::snprintf(buffer, sizeof(buffer), "%c", static_cast<char>(c & 0xFFU));
            return std::string("'") + buffer + "'";
        }
        case TypeCode_String:
            return "\"" + UnityStringToUtf8(reinterpret_cast<Unity::System_String*>(retObject)) + "\"";
        case TypeCode_Boolean:
            return readBoxed((bool*)nullptr) ? "true" : "false";
        default:
            std::snprintf(buffer, sizeof(buffer), "%p", retObject);
            return buffer;
        }
    }

    static bool InvokeMethodViaRuntimeInvoke(
        ExplorerState& state,
        Unity::CComponent* component,
        Unity::il2cppMethodInfo* method,
        const std::vector<std::string>& argTexts,
        std::string* outResult)
    {
        if (!state.fnRuntimeInvoke || !method || !outResult)
            return false;

        Unity::il2cppType* returnTypeInfo = nullptr;
        uint32_t argCount = 0;
        uint32_t methodFlags = 0;
        if (!SafeReadMethodMetadata(method, nullptr, &returnTypeInfo, nullptr, nullptr, &argCount, &methodFlags))
        {
            *outResult = "invalid method metadata";
            return false;
        }

        if (argCount > kMaxMethodArgsInvoke || argCount > kHardMethodArgsLimit)
        {
            *outResult = "too many args";
            return false;
        }

        if (argTexts.size() != argCount)
            return false;

        RuntimeInvokeFn runtimeInvoke = reinterpret_cast<RuntimeInvokeFn>(state.fnRuntimeInvoke);
        if (!runtimeInvoke)
            return false;

        std::vector<MethodArgValue> parsedArgs(argCount);
        std::vector<void*> invokeArgs(argCount, nullptr);

        for (uint32_t i = 0; i < argCount; ++i)
        {
            Unity::il2cppType* paramType = SafeGetMethodParamType(method, i);
            const unsigned int typeCode = GetFieldTypeEnum(paramType);

            if (!ParseMethodArgument(typeCode, argTexts[i], &parsedArgs[i]))
            {
                *outResult = std::string("arg parse failed #") + std::to_string(i);
                return false;
            }

            void* ptr = GetMethodArgumentPointer(typeCode, &parsedArgs[i]);
            if (!ptr && typeCode != TypeCode_Void)
            {
                *outResult = std::string("unsupported arg type #") + std::to_string(i);
                return false;
            }

            invokeArgs[i] = ptr;
        }

        void* exception = nullptr;
        bool invokeFaulted = false;
        Unity::il2cppObject* ret = SafeRuntimeInvokeCall(
            runtimeInvoke,
            method,
            ((methodFlags & 0x0010U) != 0U) ? nullptr : component,
            invokeArgs.empty() ? nullptr : invokeArgs.data(),
            &exception,
            &invokeFaulted);

        if (invokeFaulted)
        {
            *outResult = "<runtime invoke fault>";
            return false;
        }

        if (exception)
        {
            *outResult = "<runtime exception>";
            return false;
        }

        const unsigned int returnType = GetFieldTypeEnum(returnTypeInfo);
        *outResult = FormatRuntimeInvokeReturn(returnType, ret);
        return true;
    }

    static void SyncTransformEditor(ExplorerState& state, Unity::CGameObject* gameObject)
    {
        state.transformEditTarget = gameObject;

        if (!gameObject)
            return;

        Unity::CTransform* transform = SafeGetTransform(gameObject);
        if (!transform)
            return;

        Unity::Vector3 localPosition{};
        Unity::Vector3 localScale{ 1.0f, 1.0f, 1.0f };
        Unity::Vector3 euler{};

        __try
        {
            localPosition = transform->GetLocalPosition();
            localScale = transform->GetLocalScale();
            euler = transform->GetRotation().ToEuler();
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            HBLog::Printf("[UExplorer] EXCEPTION: reading transform values failed for %p.\n", gameObject);
            return;
        }

        state.editLocalPosition[0] = localPosition.x;
        state.editLocalPosition[1] = localPosition.y;
        state.editLocalPosition[2] = localPosition.z;

        state.editEuler[0] = euler.x;
        state.editEuler[1] = euler.y;
        state.editEuler[2] = euler.z;

        state.editLocalScale[0] = localScale.x;
        state.editLocalScale[1] = localScale.y;
        state.editLocalScale[2] = localScale.z;
    }

    static void RefreshSceneCache(ExplorerState& state)
    {
        state.scenes.clear();

        const int sceneCount = Unity::SceneManager::GetSceneCount();
        for (int i = 0; i < sceneCount; ++i)
        {
            const Unity::Scene scene = Unity::SceneManager::GetSceneAt(i);

            SceneEntry entry{};
            entry.scene = scene;

            char labelBuffer[64]{};
            std::snprintf(labelBuffer, sizeof(labelBuffer), "Scene %d [handle=%d]", i, scene.m_Handle);
            entry.label = labelBuffer;

            state.scenes.emplace_back(std::move(entry));
        }

        bool selectedSceneExists = (state.selectedSceneHandle == 0);
        if (!selectedSceneExists)
        {
            for (const SceneEntry& scene : state.scenes)
            {
                if (scene.scene.m_Handle == state.selectedSceneHandle)
                {
                    selectedSceneExists = true;
                    break;
                }
            }
        }

        if (!selectedSceneExists)
            state.selectedSceneHandle = 0;

        if (state.lastSceneCountLogged != sceneCount)
        {
            HBLog::Printf("[UExplorer] Scene cache refreshed: %d scene(s).\n", sceneCount);
            state.lastSceneCountLogged = sceneCount;
        }

        state.lastSceneRefreshTick = GetTickCount64();
    }

    static void RefreshObjectCache(ExplorerState& state)
    {
        state.objects.clear();
        state.indexByTransform.clear();
        state.rootTransforms.clear();
        state.aliveObjects.clear();
        state.classBlobCacheLower.clear();

        Unity::il2cppArray<Unity::CGameObject*>* allGameObjects = SafeFindGameObjects(state.includeInactive);

        if (!allGameObjects)
        {
            state.lastObjectRefreshTick = GetTickCount64();
            return;
        }

        state.objects.reserve(static_cast<size_t>(allGameObjects->m_uMaxLength));

        for (uintptr_t i = 0; i < allGameObjects->m_uMaxLength; ++i)
        {
            const unsigned int idx = static_cast<unsigned int>(i);
            Unity::CGameObject* gameObject = SafeArrayGetGameObject(allGameObjects, idx);
            if (!gameObject)
                continue;

            Unity::CTransform* transform = SafeGetTransform(gameObject);
            if (!transform)
                continue;

            ObjectEntry entry{};
            entry.gameObject = gameObject;
            entry.transform = transform;
            entry.parent = SafeGetParent(transform);
            entry.sceneHandle = GetSceneHandleForGameObject(state, gameObject);
            entry.name = SafeGetObjectName(gameObject);
            entry.nameLower = ToLowerCopy(entry.name);

            state.aliveObjects.insert(gameObject);
            state.indexByTransform[entry.transform] = state.objects.size();
            state.objects.emplace_back(std::move(entry));
        }

        state.rootTransforms.clear();
        for (size_t i = 0; i < state.objects.size(); ++i)
        {
            ObjectEntry& entry = state.objects[i];
            auto parentIt = state.indexByTransform.find(entry.parent);
            if (entry.parent && parentIt != state.indexByTransform.end())
            {
                state.objects[parentIt->second].children.emplace_back(entry.transform);
            }
            else
            {
                state.rootTransforms.emplace_back(entry.transform);
            }
        }

        auto nameLess = [&](Unity::CTransform* left, Unity::CTransform* right)
            {
                auto l = state.indexByTransform.find(left);
                auto r = state.indexByTransform.find(right);
                if (l == state.indexByTransform.end() || r == state.indexByTransform.end())
                    return left < right;

                return _stricmp(state.objects[l->second].name.c_str(), state.objects[r->second].name.c_str()) < 0;
            };

        std::sort(state.rootTransforms.begin(), state.rootTransforms.end(), nameLess);
        for (ObjectEntry& entry : state.objects)
            std::sort(entry.children.begin(), entry.children.end(), nameLess);

        if (state.selectedObject && state.aliveObjects.find(state.selectedObject) == state.aliveObjects.end())
        {
            state.selectedObject = nullptr;
            state.transformEditTarget = nullptr;
        }

        if (state.lastObjectCountLogged != state.objects.size())
        {
            HBLog::Printf("[UExplorer] Object cache refreshed: %zu object(s).\n", state.objects.size());
            state.lastObjectCountLogged = state.objects.size();
        }

        state.lastObjectRefreshTick = GetTickCount64();
    }

    static void ForceRefresh(ExplorerState& state)
    {
        RefreshSceneCache(state);
        RefreshObjectCache(state);
    }

    static void TickRefresh(ExplorerState& state)
    {
        if (!state.fnObjectGetInstanceId)
            ResolveRuntimeMethods(state);

        if (!state.autoRefresh)
            return;

        const ULONGLONG now = GetTickCount64();
        if ((now - state.lastSceneRefreshTick) >= state.refreshIntervalMs)
            RefreshSceneCache(state);

        if ((now - state.lastObjectRefreshTick) >= state.refreshIntervalMs)
            RefreshObjectCache(state);
    }

    static bool SceneFilterPasses(const ExplorerState& state, const ObjectEntry& entry)
    {
        if (state.selectedSceneHandle == 0 || entry.sceneHandle == 0)
            return true;

        return entry.sceneHandle == state.selectedSceneHandle;
    }

    static bool ContainsLower(const std::string& haystackLower, const std::string& needleLower)
    {
        if (needleLower.empty())
            return true;

        return haystackLower.find(needleLower) != std::string::npos;
    }

    static bool HierarchyNodeVisibleRecursive(
        ExplorerState& state,
        Unity::CTransform* transform,
        const std::string& nameFilterLower,
        std::unordered_map<Unity::CTransform*, bool>& visibilityCache)
    {
        auto cacheIt = visibilityCache.find(transform);
        if (cacheIt != visibilityCache.end())
            return cacheIt->second;

        auto it = state.indexByTransform.find(transform);
        if (it == state.indexByTransform.end())
        {
            visibilityCache[transform] = false;
            return false;
        }

        ObjectEntry& entry = state.objects[it->second];
        const bool selfVisible = SceneFilterPasses(state, entry) && ContainsLower(entry.nameLower, nameFilterLower);
        if (selfVisible)
        {
            visibilityCache[transform] = true;
            return true;
        }

        for (Unity::CTransform* childTransform : entry.children)
        {
            if (HierarchyNodeVisibleRecursive(state, childTransform, nameFilterLower, visibilityCache))
            {
                visibilityCache[transform] = true;
                return true;
            }
        }

        visibilityCache[transform] = false;
        return false;
    }

    static void DrawHierarchyNode(
        ExplorerState& state,
        Unity::CTransform* transform,
        const std::string& nameFilterLower,
        std::unordered_map<Unity::CTransform*, bool>& visibilityCache)
    {
        auto it = state.indexByTransform.find(transform);
        if (it == state.indexByTransform.end())
            return;

        ObjectEntry& entry = state.objects[it->second];
        if (!HierarchyNodeVisibleRecursive(state, transform, nameFilterLower, visibilityCache))
            return;

        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick;
        if (entry.children.empty())
            flags |= ImGuiTreeNodeFlags_Leaf;

        if (state.selectedObject == entry.gameObject)
            flags |= ImGuiTreeNodeFlags_Selected;

        const bool opened = ImGui::TreeNodeEx(reinterpret_cast<void*>(entry.transform), flags, "%s", entry.name.c_str());
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
            SelectObjectDirect(state, entry.gameObject);

        if (opened)
        {
            for (Unity::CTransform* childTransform : entry.children)
                DrawHierarchyNode(state, childTransform, nameFilterLower, visibilityCache);

            ImGui::TreePop();
        }
    }

    static std::string BuildClassBlobLower(Unity::CGameObject* gameObject)
    {
        if (!gameObject)
            return {};

        Unity::il2cppObject* componentType = IL2CPP::SystemTypeCache::Get(UNITY_COMPONENT_CLASS);
        if (!componentType)
            componentType = IL2CPP::Class::GetSystemType(UNITY_COMPONENT_CLASS);

        if (!componentType)
            return {};

        Unity::il2cppArray<Unity::CComponent*>* components = SafeGetComponents(gameObject, componentType);
        if (!components)
            return {};

        std::string classBlob;
        for (uintptr_t i = 0; i < components->m_uMaxLength; ++i)
        {
            Unity::CComponent* component = components->operator[](static_cast<unsigned int>(i));
            if (!component || !component->m_Object.m_pClass)
                continue;

            if (!classBlob.empty())
                classBlob.push_back(' ');

            classBlob += ToLowerCopy(GetClassDisplayName(component->m_Object.m_pClass));
        }

        return classBlob;
    }

    static bool MatchesClassFilter(ExplorerState& state, Unity::CGameObject* gameObject, const std::string& classFilterLower)
    {
        if (classFilterLower.empty())
            return true;

        auto cachedIt = state.classBlobCacheLower.find(gameObject);
        if (cachedIt == state.classBlobCacheLower.end())
        {
            std::string classBlob = BuildClassBlobLower(gameObject);
            cachedIt = state.classBlobCacheLower.emplace(gameObject, std::move(classBlob)).first;
        }

        return ContainsLower(cachedIt->second, classFilterLower);
    }

    static void DrawSceneLoader(ExplorerState& state)
    {
        ImGui::SeparatorText("Scene Loader");
        ImGui::InputText("Scene name", state.sceneToLoad, IM_ARRAYSIZE(state.sceneToLoad));

        if (AnimatedButton("Load (Single)", ImVec2(-1.0f, 0.0f)))
        {
            if (state.sceneToLoad[0] != '\0')
            {
                Unity::SceneManager::LoadScene(state.sceneToLoad, Unity::LoadSceneMode::Single);
                HBLog::Printf("[UExplorer] LoadScene single: %s\n", state.sceneToLoad);
                ForceRefresh(state);
            }
        }

        if (AnimatedButton("Load (Additive)", ImVec2(-1.0f, 0.0f)))
        {
            if (state.sceneToLoad[0] != '\0')
            {
                Unity::SceneManager::LoadScene(state.sceneToLoad, Unity::LoadSceneMode::Additive);
                HBLog::Printf("[UExplorer] LoadScene additive: %s\n", state.sceneToLoad);
                ForceRefresh(state);
            }
        }
    }

    static void DrawSceneExplorerTab(ExplorerState& state)
    {
        ImGui::Checkbox("Auto refresh", &state.autoRefresh);
        ImGui::SameLine();
        if (ImGui::Checkbox("Include inactive", &state.includeInactive))
            ForceRefresh(state);

        int refreshMs = static_cast<int>(state.refreshIntervalMs);
        if (ImGui::SliderInt("Refresh (ms)", &refreshMs, 250, 5000))
            state.refreshIntervalMs = static_cast<ULONGLONG>(refreshMs);

        if (AnimatedButton("Refresh now"))
            ForceRefresh(state);

        const char* selectedSceneLabel = "All scenes";
        for (const SceneEntry& scene : state.scenes)
        {
            if (scene.scene.m_Handle == state.selectedSceneHandle)
            {
                selectedSceneLabel = scene.label.c_str();
                break;
            }
        }

        if (ImGui::BeginCombo("Scene", selectedSceneLabel))
        {
            if (ImGui::Selectable("All scenes", state.selectedSceneHandle == 0))
                state.selectedSceneHandle = 0;

            for (const SceneEntry& scene : state.scenes)
            {
                const bool selected = (state.selectedSceneHandle == scene.scene.m_Handle);
                if (ImGui::Selectable(scene.label.c_str(), selected))
                    state.selectedSceneHandle = scene.scene.m_Handle;
            }

            ImGui::EndCombo();
        }

        ImGui::InputTextWithHint("##hierarchy_filter", "Search object...", state.hierarchyFilter, IM_ARRAYSIZE(state.hierarchyFilter));

        const std::string filterLower = ToLowerCopy(state.hierarchyFilter);
        std::unordered_map<Unity::CTransform*, bool> visibilityCache;
        visibilityCache.reserve(state.objects.size());
        const bool unfilteredView = (filterLower.empty() && state.selectedSceneHandle == 0);

        ImGui::BeginChild("HierarchyTree", ImVec2(0.0f, -140.0f), true);
        if (unfilteredView)
        {
            ImGuiListClipper clipper;
            clipper.Begin(static_cast<int>(state.rootTransforms.size()));
            while (clipper.Step())
            {
                for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
                {
                    DrawHierarchyNode(state, state.rootTransforms[static_cast<size_t>(row)], filterLower, visibilityCache);
                }
            }
        }
        else
        {
            for (Unity::CTransform* rootTransform : state.rootTransforms)
                DrawHierarchyNode(state, rootTransform, filterLower, visibilityCache);
        }
        ImGui::EndChild();

        DrawSceneLoader(state);
    }

    static void DrawObjectSearchTab(ExplorerState& state)
    {
        ImGui::InputTextWithHint("Class filter", "e.g. UnityEngine.Camera", state.classFilter, IM_ARRAYSIZE(state.classFilter));
        ImGui::InputTextWithHint("Name contains", "e.g. Main", state.nameFilter, IM_ARRAYSIZE(state.nameFilter));

        if (AnimatedButton("Refresh results"))
            ForceRefresh(state);

        const std::string classFilterLower = ToLowerCopy(state.classFilter);
        const std::string nameFilterLower = ToLowerCopy(state.nameFilter);

        std::vector<size_t> matches;
        matches.reserve(state.objects.size());

        for (size_t i = 0; i < state.objects.size(); ++i)
        {
            ObjectEntry& entry = state.objects[i];

            if (!SceneFilterPasses(state, entry))
                continue;

            if (!ContainsLower(entry.nameLower, nameFilterLower))
                continue;

            if (!MatchesClassFilter(state, entry.gameObject, classFilterLower))
                continue;

            matches.emplace_back(i);
        }

        ImGui::Text("Results: %zu", matches.size());
        ImGui::Separator();

        ImGui::BeginChild("ObjectSearchResults", ImVec2(0.0f, 0.0f), true);
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(matches.size()));

        while (clipper.Step())
        {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
            {
                ObjectEntry& entry = state.objects[matches[static_cast<size_t>(row)]];
                const bool selected = (state.selectedObject == entry.gameObject);

                std::string label = entry.name + "##" + std::to_string(reinterpret_cast<uintptr_t>(entry.gameObject));
                if (ImGui::Selectable(label.c_str(), selected))
                    SelectObjectDirect(state, entry.gameObject);
            }
        }

        ImGui::EndChild();
    }

    template<typename T>
    static bool ReadFieldValue(Unity::CComponent* component, Unity::il2cppFieldInfo* field, bool isStatic, T* outValue)
    {
        if (!component || !field || !outValue)
            return false;

        if (isStatic)
            return TryGetStaticFieldValue(field, outValue);

        __try
        {
            *outValue = component->GetMemberValue<T>(field);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    template<typename T>
    static bool WriteFieldValue(Unity::CComponent* component, Unity::il2cppFieldInfo* field, bool isStatic, T value)
    {
        if (!component || !field)
            return false;

        if (isStatic)
            return TrySetStaticFieldValue(field, &value);

        __try
        {
            component->SetMemberValue<T>(field, value);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    static bool IsSupportedEditableType(unsigned int typeCode)
    {
        switch (typeCode)
        {
        case TypeCode_Boolean:
        case TypeCode_String:
        case TypeCode_R4:
        case TypeCode_R8:
        case TypeCode_I4:
        case TypeCode_U4:
        case TypeCode_I2:
        case TypeCode_U2:
        case TypeCode_I1:
        case TypeCode_U1:
        case TypeCode_Char:
            return true;
        default:
            return false;
        }
    }

    static std::string BuildFieldDraftFromCurrentValue(
        Unity::CComponent* component,
        Unity::il2cppFieldInfo* field,
        bool isStatic,
        unsigned int typeCode)
    {
        char buffer[128]{};

        switch (typeCode)
        {
        case TypeCode_Boolean:
        {
            bool value = false;
            ReadFieldValue(component, field, isStatic, &value);
            return value ? "true" : "false";
        }
        case TypeCode_I4:
        {
            int value = 0;
            ReadFieldValue(component, field, isStatic, &value);
            std::snprintf(buffer, sizeof(buffer), "%d", value);
            return buffer;
        }
        case TypeCode_U4:
        {
            uint32_t value = 0;
            ReadFieldValue(component, field, isStatic, &value);
            std::snprintf(buffer, sizeof(buffer), "%u", value);
            return buffer;
        }
        case TypeCode_R4:
        {
            float value = 0.0f;
            ReadFieldValue(component, field, isStatic, &value);
            std::snprintf(buffer, sizeof(buffer), "%.6f", value);
            return buffer;
        }
        case TypeCode_R8:
        {
            double value = 0.0;
            ReadFieldValue(component, field, isStatic, &value);
            std::snprintf(buffer, sizeof(buffer), "%.9f", value);
            return buffer;
        }
        case TypeCode_I2:
        {
            short value = 0;
            ReadFieldValue(component, field, isStatic, &value);
            std::snprintf(buffer, sizeof(buffer), "%d", static_cast<int>(value));
            return buffer;
        }
        case TypeCode_U2:
        {
            uint16_t value = 0;
            ReadFieldValue(component, field, isStatic, &value);
            std::snprintf(buffer, sizeof(buffer), "%u", static_cast<unsigned int>(value));
            return buffer;
        }
        case TypeCode_I1:
        {
            int8_t value = 0;
            ReadFieldValue(component, field, isStatic, &value);
            std::snprintf(buffer, sizeof(buffer), "%d", static_cast<int>(value));
            return buffer;
        }
        case TypeCode_U1:
        {
            unsigned char value = 0;
            ReadFieldValue(component, field, isStatic, &value);
            std::snprintf(buffer, sizeof(buffer), "%u", static_cast<unsigned int>(value));
            return buffer;
        }
        case TypeCode_Char:
        {
            uint16_t value = 0;
            ReadFieldValue(component, field, isStatic, &value);
            const char printable = static_cast<char>(value & 0xFFU);
            if (printable >= 0x20 && printable <= 0x7E)
                std::snprintf(buffer, sizeof(buffer), "%c", printable);
            else
                std::snprintf(buffer, sizeof(buffer), "%u", static_cast<unsigned int>(value));
            return buffer;
        }
        case TypeCode_String:
        {
            Unity::System_String* value = nullptr;
            ReadFieldValue(component, field, isStatic, &value);
            return UnityStringToUtf8(value);
        }
        default:
            return {};
        }
    }

    static bool ApplyFieldDraftValue(
        Unity::CComponent* component,
        Unity::il2cppFieldInfo* field,
        bool isStatic,
        unsigned int typeCode,
        const std::string& textDraft)
    {
        switch (typeCode)
        {
        case TypeCode_Boolean:
        {
            bool value = false;
            if (!ParseBoolText(textDraft, &value))
                return false;
            return WriteFieldValue(component, field, isStatic, value);
        }
        case TypeCode_I4:
        {
            int value = 0;
            if (!ParseSignedIntegerText<int>(textDraft, &value))
                return false;
            return WriteFieldValue(component, field, isStatic, value);
        }
        case TypeCode_U4:
        {
            uint32_t value = 0;
            if (!ParseUnsignedIntegerText<uint32_t>(textDraft, &value))
                return false;
            return WriteFieldValue(component, field, isStatic, value);
        }
        case TypeCode_R4:
        {
            float value = 0.0f;
            if (!ParseFloatText(textDraft, &value))
                return false;
            return WriteFieldValue(component, field, isStatic, value);
        }
        case TypeCode_R8:
        {
            double value = 0.0;
            if (!ParseDoubleText(textDraft, &value))
                return false;
            return WriteFieldValue(component, field, isStatic, value);
        }
        case TypeCode_I2:
        {
            short value = 0;
            if (!ParseSignedIntegerText<short>(textDraft, &value))
                return false;
            return WriteFieldValue(component, field, isStatic, value);
        }
        case TypeCode_U2:
        {
            uint16_t value = 0;
            if (!ParseUnsignedIntegerText<uint16_t>(textDraft, &value))
                return false;
            return WriteFieldValue(component, field, isStatic, value);
        }
        case TypeCode_I1:
        {
            int8_t value = 0;
            if (!ParseSignedIntegerText<int8_t>(textDraft, &value))
                return false;
            return WriteFieldValue(component, field, isStatic, value);
        }
        case TypeCode_U1:
        {
            unsigned char value = 0;
            if (!ParseUnsignedIntegerText<unsigned char>(textDraft, &value))
                return false;
            return WriteFieldValue(component, field, isStatic, value);
        }
        case TypeCode_Char:
        {
            uint16_t value = 0;
            if (!ParseCharText(textDraft, &value))
                return false;
            return WriteFieldValue(component, field, isStatic, value);
        }
        case TypeCode_String:
        {
            Unity::System_String* value = IL2CPP::String::New(textDraft.c_str());
            if (!value)
                return false;
            return WriteFieldValue<Unity::System_String*>(component, field, isStatic, value);
        }
        default:
            return false;
        }
    }

    static bool IsSupportedMethodArgType(unsigned int typeCode)
    {
        return IsSupportedEditableType(typeCode);
    }

    static bool IsInspectableReferenceType(unsigned int typeCode)
    {
        switch (typeCode)
        {
        case TypeCode_Object:
        case TypeCode_Class:
        case Unity::Type_Array:
        case Unity::Type_Variable:
            return true;
        default:
            return false;
        }
    }

    static std::string BuildDefaultMethodArgDraft(unsigned int typeCode)
    {
        switch (typeCode)
        {
        case TypeCode_Boolean:
            return "false";
        case TypeCode_String:
            return {};
        case TypeCode_R4:
        case TypeCode_R8:
            return "0.0";
        case TypeCode_Char:
            return "A";
        default:
            return "0";
        }
    }

    static void DrawComponentFields(ExplorerState& state, Unity::CComponent* component)
    {
        std::vector<Unity::il2cppFieldInfo*> fields;
        component->FetchFields(&fields);

        if (fields.empty())
        {
            ImGui::TextDisabled("No fields");
            return;
        }

        constexpr size_t kMaxDrawFields = 128;
        const size_t drawCount = (fields.size() > kMaxDrawFields) ? kMaxDrawFields : fields.size();

        for (size_t i = 0; i < drawCount; ++i)
        {
            Unity::il2cppFieldInfo* field = fields[i];
            if (!field || !field->m_pName)
                continue;

            const std::string fieldName = MakeSafeMemberLabel(field->m_pName, "field", field);
            const std::string typeName = ClampUiLabel(GetFieldTypeName(field->m_pType), kMaxUiLabelChars);
            const bool isStatic = IsStaticField(field);
            const unsigned int typeEnum = GetFieldTypeEnum(field->m_pType);
            const uint64_t fieldKey = BuildFieldKey(component, field);

            ImGui::PushID(field);
            const std::string fieldHeader = typeName + " " + fieldName + (isStatic ? " [static]" : " [instance]");
            ImGui::PushStyleColor(ImGuiCol_Text, kColorField);
            const bool fieldOpen = ImGui::TreeNode(fieldHeader.c_str());
            ImGui::PopStyleColor();

            if (fieldOpen)
            {
                ImGui::TextDisabled("field_info=%p  offset=0x%X", field, field->m_iOffset);
                if (!isStatic && field->m_iOffset >= 0)
                {
                    void* fieldAddress = reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(component) + static_cast<uintptr_t>(field->m_iOffset));
                    ImGui::TextDisabled("instance_ptr=%p", fieldAddress);
                }
                else if (isStatic)
                {
                    ImGui::TextDisabled("instance_ptr=<static>");
                    ImGui::TextDisabled("storage=il2cpp_field_static_*");
                }

                if (IsSupportedEditableType(typeEnum))
                {
                    if (typeEnum == TypeCode_Boolean)
                    {
                        bool value = false;
                        ReadFieldValue(component, field, isStatic, &value);

                        if (ImGui::Checkbox("Value", &value))
                        {
                            const bool ok = WriteFieldValue(component, field, isStatic, value);
                            HBLog::Printf("[UExplorer] Field bool apply %s: %s.%s = %s (%s)\n",
                                ok ? "OK" : "FAILED",
                                component->m_Object.m_pClass ? component->m_Object.m_pClass->m_pName : "<class>",
                                fieldName.c_str(),
                                value ? "true" : "false",
                                isStatic ? "static" : "instance");
                        }
                    }
                    else
                    {
                        auto draftIt = state.fieldValueDrafts.find(fieldKey);
                        if (draftIt == state.fieldValueDrafts.end())
                            draftIt = state.fieldValueDrafts.emplace(fieldKey, BuildFieldDraftFromCurrentValue(component, field, isStatic, typeEnum)).first;

                        std::string currentValueText = BuildFieldDraftFromCurrentValue(component, field, isStatic, typeEnum);

                        char buffer[512]{};
                        strncpy_s(buffer, IM_ARRAYSIZE(buffer), draftIt->second.c_str(), _TRUNCATE);
                        ImGui::SetNextItemWidth(280.0f);
                        if (ImGui::InputText("Value", buffer, IM_ARRAYSIZE(buffer)))
                            draftIt->second = buffer;

                        ImGui::SameLine();
                        if (AnimatedButton("Apply"))
                        {
                            if (ApplyFieldDraftValue(component, field, isStatic, typeEnum, draftIt->second))
                            {
                                draftIt->second = BuildFieldDraftFromCurrentValue(component, field, isStatic, typeEnum);
                                HBLog::Printf("[UExplorer] Field apply OK: %s.%s = %s (%s)\n",
                                    component->m_Object.m_pClass ? component->m_Object.m_pClass->m_pName : "<class>",
                                    fieldName.c_str(),
                                    draftIt->second.c_str(),
                                    isStatic ? "static" : "instance");
                            }
                            else
                            {
                                HBLog::Printf("[UExplorer] Field apply FAILED: %s.%s input='%s'\n",
                                    component->m_Object.m_pClass ? component->m_Object.m_pClass->m_pName : "<class>",
                                    fieldName.c_str(),
                                    draftIt->second.c_str());
                            }
                        }

                        ImGui::SameLine();
                        ImGui::TextDisabled("current: %s", currentValueText.c_str());
                    }
                }
                else if (IsInspectableReferenceType(typeEnum))
                {
                    FieldReferencePreview& preview = state.fieldReferencePreviews[fieldKey];
                    if (AnimatedButton(preview.loaded ? "Refresh Ref" : "Read Ref"))
                    {
                        Unity::il2cppObject* refValue = nullptr;
                        preview.loaded = true;
                        preview.readFailed = !ReadFieldValue(component, field, isStatic, &refValue);
                        preview.pointerValue = refValue ? reinterpret_cast<uintptr_t>(refValue) : 0;

                        if (preview.readFailed)
                        {
                            HBLog::Printf("[UExplorer] Read ref FAILED: %s.%s (%s)\n",
                                component->m_Object.m_pClass ? component->m_Object.m_pClass->m_pName : "<class>",
                                fieldName.c_str(),
                                isStatic ? "static" : "instance");
                        }
                    }

                    if (!preview.loaded)
                    {
                        ImGui::TextDisabled("Reference is not read yet.");
                    }
                    else if (preview.readFailed)
                    {
                        ImGui::TextDisabled("Reference read failed.");
                    }
                    else if (preview.pointerValue == 0)
                    {
                        ImGui::TextDisabled("ref=<null>");
                    }
                    else
                    {
                        Unity::il2cppObject* refValue = reinterpret_cast<Unity::il2cppObject*>(preview.pointerValue);
                        ImGui::Text("ref=%p", refValue);

                        if (AnimatedButton("Inspect Ref"))
                        {
                            Unity::CGameObject* refObject = ResolveInspectableGameObjectFromCache(state, refValue);
                            if (refObject)
                            {
                                NavigateToReferencedObject(state, refObject);
                                HBLog::Printf("[UExplorer] Navigate reference: %s -> %s\n",
                                    fieldName.c_str(),
                                    SafeGetObjectName(refObject).c_str());
                            }
                            else
                            {
                                HBLog::Printf("[UExplorer] Inspect Ref failed: %s (ref=%p)\n",
                                    fieldName.c_str(),
                                    refValue);
                            }
                        }
                        ImGui::SameLine();
                        ImGui::TextDisabled("Only GameObject/Component refs are navigable.");
                    }
                }
                else
                {
                    void* pointerValue = nullptr;
                    ReadFieldValue(component, field, isStatic, &pointerValue);
                    ImGui::TextDisabled("value_ptr=%p", pointerValue);
                }

                ImGui::TreePop();
            }

            ImGui::Separator();
            ImGui::PopID();
        }

        if (fields.size() > drawCount)
            ImGui::TextDisabled("... %zu more field(s)", fields.size() - drawCount);
    }

    static void DrawComponentMethods(ExplorerState& state, Unity::CComponent* component)
    {
        std::vector<Unity::il2cppMethodInfo*> methods;
        if (!SafeFetchMethods(component, &methods))
        {
            ImGui::TextDisabled("Method metadata unavailable");
            return;
        }

        if (methods.empty())
        {
            ImGui::TextDisabled("No methods");
            return;
        }

        constexpr size_t kMaxDrawMethods = 256;
        const size_t drawCount = (methods.size() > kMaxDrawMethods) ? kMaxDrawMethods : methods.size();

        for (size_t i = 0; i < drawCount; ++i)
        {
            Unity::il2cppMethodInfo* method = methods[i];
            if (!method)
                continue;

            const char* rawMethodName = nullptr;
            Unity::il2cppType* returnType = nullptr;
            void* methodPointer = nullptr;
            void* invokerPointer = nullptr;
            uint32_t rawArgCount = 0;
            uint32_t methodFlags = 0;
            if (!SafeReadMethodMetadata(
                method,
                &rawMethodName,
                &returnType,
                &methodPointer,
                &invokerPointer,
                &rawArgCount,
                &methodFlags))
            {
                ImGui::PushID(method);
                ImGui::TextDisabled("<invalid method metadata>");
                ImGui::Separator();
                ImGui::PopID();
                continue;
            }

            const uint32_t uiArgCount = (rawArgCount > kMaxMethodArgsUi) ? kMaxMethodArgsUi : rawArgCount;
            const bool argCountCapped = (rawArgCount > kMaxMethodArgsUi);
            const bool suspiciousArgCount = (rawArgCount > kHardMethodArgsLimit);
            const bool argCountTooLargeForInvoke = (rawArgCount > kMaxMethodArgsInvoke) || suspiciousArgCount;
            const uint32_t displayedArgCount = suspiciousArgCount ? 0U : uiArgCount;

            const std::string methodName = MakeSafeMemberLabel(rawMethodName, "method", method);
            const bool isStatic = (methodFlags & 0x0010U) != 0U;
            const uint64_t methodKey = BuildMethodKey(component, method);
            const std::string signature = BuildMethodSignature(method, displayedArgCount, rawArgCount);
            const std::string returnTypeName = ClampUiLabel(GetFieldTypeName(returnType), kMaxUiLabelChars);

            ImGui::PushID(method);
            ImGui::TextColored(kColorMethod, "%s %s", returnTypeName.c_str(), signature.c_str());
            ImGui::SameLine();
            ImGui::TextDisabled("[%s]", isStatic ? "static" : "instance");
            ImGui::TextDisabled("method_info=%p", method);
            uintptr_t methodRva = 0;
            if (TryGetGameAssemblyRva(methodPointer, &methodRva))
                ImGui::TextDisabled("method_ptr=%p  RVA=0x%llX", methodPointer, static_cast<unsigned long long>(methodRva));
            else
                ImGui::TextDisabled("method_ptr=%p", methodPointer);

            uintptr_t invokerRva = 0;
            if (TryGetGameAssemblyRva(invokerPointer, &invokerRva))
                ImGui::TextDisabled("invoker=%p  RVA=0x%llX", invokerPointer, static_cast<unsigned long long>(invokerRva));
            else
                ImGui::TextDisabled("invoker=%p", invokerPointer);

            auto draftIt = state.methodArgDrafts.find(methodKey);
            if (draftIt == state.methodArgDrafts.end())
                draftIt = state.methodArgDrafts.emplace(methodKey, std::vector<std::string>{}).first;

            if (draftIt->second.size() != displayedArgCount)
                draftIt->second.assign(displayedArgCount, {});

            bool allArgsSupported = !argCountTooLargeForInvoke;
            for (uint32_t argIndex = 0; argIndex < displayedArgCount; ++argIndex)
            {
                Unity::il2cppType* paramType = SafeGetMethodParamType(method, argIndex);
                const unsigned int typeCode = GetFieldTypeEnum(paramType);
                const bool isSupported = IsSupportedMethodArgType(typeCode);
                if (!isSupported)
                    allArgsSupported = false;

                const char* paramNameRaw = SafeGetMethodParamName(method, argIndex);
                std::string paramName = paramNameRaw ? SanitizeMemberName(paramNameRaw) : ("arg" + std::to_string(argIndex));
                if (paramName == "<invalid-name>" || paramName == "<null>" || paramName.empty())
                    paramName = "arg" + std::to_string(argIndex);

                if (draftIt->second[argIndex].empty())
                    draftIt->second[argIndex] = BuildDefaultMethodArgDraft(typeCode);

                const std::string safeParamTypeName = ClampUiLabel(GetFieldTypeName(paramType), kMaxUiLabelChars);
                ImGui::Text("arg%u: %s %s", static_cast<unsigned int>(argIndex), safeParamTypeName.c_str(), paramName.c_str());
                ImGui::SameLine();

                if (isSupported)
                {
                    const std::string inputId = "##arg_" + std::to_string(argIndex);
                    if (typeCode == TypeCode_Boolean)
                    {
                        bool boolValue = false;
                        ParseBoolText(draftIt->second[argIndex], &boolValue);
                        if (ImGui::Checkbox(inputId.c_str(), &boolValue))
                            draftIt->second[argIndex] = boolValue ? "true" : "false";
                    }
                    else
                    {
                        char argBuffer[256]{};
                        strncpy_s(argBuffer, IM_ARRAYSIZE(argBuffer), draftIt->second[argIndex].c_str(), _TRUNCATE);
                        ImGui::SetNextItemWidth(240.0f);
                        if (ImGui::InputText(inputId.c_str(), argBuffer, IM_ARRAYSIZE(argBuffer)))
                            draftIt->second[argIndex] = argBuffer;
                    }
                }
                else
                {
                    ImGui::TextDisabled("unsupported arg type");
                }
            }

            if (suspiciousArgCount)
            {
                ImGui::TextDisabled("Metadata guard: argument list hidden (%u args).", static_cast<unsigned int>(rawArgCount));
            }
            else if (argCountCapped)
            {
                ImGui::TextDisabled("... %u more arg(s) hidden", static_cast<unsigned int>(rawArgCount - uiArgCount));
            }

            const bool canInvoke = (state.fnRuntimeInvoke != nullptr) && allArgsSupported;
            ImGui::BeginDisabled(!canInvoke);
            if (AnimatedButton("Invoke"))
            {
                std::string invokeResult;
                if (InvokeMethodViaRuntimeInvoke(state, component, method, draftIt->second, &invokeResult))
                {
                    state.methodInvokeResults[methodKey] = invokeResult;
                    HBLog::Printf("[UExplorer] Method invoke OK: %s -> %s\n", methodName.c_str(), invokeResult.c_str());
                }
                else
                {
                    if (invokeResult.empty())
                        invokeResult = "<invoke-failed>";
                    state.methodInvokeResults[methodKey] = invokeResult;
                    HBLog::Printf("[UExplorer] Method invoke FAILED: %s -> %s\n", methodName.c_str(), invokeResult.c_str());
                }
            }
            ImGui::EndDisabled();

            if (state.fnRuntimeInvoke == nullptr)
                ImGui::TextDisabled("il2cpp_runtime_invoke is not resolved.");
            else if (argCountTooLargeForInvoke)
                ImGui::TextDisabled("Invoke unavailable: method has too many args.");
            else if (!allArgsSupported)
                ImGui::TextDisabled("Invoke unavailable: unsupported argument type present.");

            auto resultIt = state.methodInvokeResults.find(methodKey);
            if (resultIt != state.methodInvokeResults.end())
            {
                ImGui::SameLine();
                ImGui::Text("Result: %s", resultIt->second.c_str());
            }

            ImGui::Separator();
            ImGui::PopID();
        }

        if (methods.size() > drawCount)
            ImGui::TextDisabled("... %zu more method(s)", methods.size() - drawCount);
    }

    static void DrawComponentsInspector(ExplorerState& state, Unity::CGameObject* gameObject)
    {
        if (!gameObject)
            return;

        Unity::il2cppObject* componentType = IL2CPP::SystemTypeCache::Get(UNITY_COMPONENT_CLASS);
        if (!componentType)
            componentType = IL2CPP::Class::GetSystemType(UNITY_COMPONENT_CLASS);

        if (!componentType)
        {
            ImGui::TextDisabled("Component type cache is not ready");
            return;
        }

        Unity::il2cppArray<Unity::CComponent*>* components = SafeGetComponents(gameObject, componentType);
        if (!components || components->m_uMaxLength == 0)
        {
            ImGui::TextDisabled("No components found");
            return;
        }

        for (uintptr_t i = 0; i < components->m_uMaxLength; ++i)
        {
            Unity::CComponent* component = components->operator[](static_cast<unsigned int>(i));
            if (!component)
                continue;

            const std::string componentName = GetClassDisplayName(component->m_Object.m_pClass);
            const std::string treeLabel = componentName + "##component_" + std::to_string(reinterpret_cast<uintptr_t>(component));

            ImGui::PushStyleColor(ImGuiCol_Text, kColorClass);
            const bool componentOpen = ImGui::TreeNode(treeLabel.c_str());
            ImGui::PopStyleColor();

            if (componentOpen)
            {
                ImGui::Text("Address: 0x%p", component);
                ImGui::TextDisabled("cached_ptr=%p", component->m_CachedPtr);

                ImGui::PushStyleColor(ImGuiCol_Text, kColorField);
                const bool fieldsOpen = ImGui::TreeNode("Fields");
                ImGui::PopStyleColor();
                if (fieldsOpen)
                {
                    DrawComponentFields(state, component);
                    ImGui::TreePop();
                }

                ImGui::PushStyleColor(ImGuiCol_Text, kColorMethod);
                const bool methodsOpen = ImGui::TreeNode("Methods");
                ImGui::PopStyleColor();
                if (methodsOpen)
                {
                    DrawComponentMethods(state, component);
                    ImGui::TreePop();
                }

                ImGui::TreePop();
            }
        }
    }

    static void DrawInspectorWindow(ExplorerState& state)
    {
        ImGui::Begin("Inspector", nullptr, ImGuiWindowFlags_NoCollapse);

        if (!state.navigationHistory.empty())
        {
            if (AnimatedButton("< Back"))
                NavigateBack(state);

            ImGui::SameLine();
            ImGui::TextDisabled("History: %zu", state.navigationHistory.size());
        }

        if (!state.selectedObject)
        {
            ImGui::TextDisabled("Select an object from Object Explorer.");
            ImGui::End();
            return;
        }

        if (state.aliveObjects.find(state.selectedObject) == state.aliveObjects.end())
        {
            ImGui::TextDisabled("Selected object is no longer valid.");
            ImGui::End();
            return;
        }

        if (state.transformEditTarget != state.selectedObject)
            SyncTransformEditor(state, state.selectedObject);

        Unity::CGameObject* gameObject = state.selectedObject;

        ImGui::Text("Name: %s", SafeGetObjectName(gameObject).c_str());
        ImGui::Text("Type: %s", GetClassDisplayName(gameObject->m_Object.m_pClass).c_str());
        ImGui::Text("Address: 0x%p", gameObject);

        if (state.fnObjectGetInstanceId)
            ImGui::Text("Instance ID: %d", GetInstanceId(state, gameObject));

        int sceneHandle = GetSceneHandleForGameObject(state, gameObject);
        if (sceneHandle != 0)
            ImGui::Text("Scene handle: %d", sceneHandle);

        bool active = gameObject->GetActive();
        if (ImGui::Checkbox("Active", &active))
            gameObject->SetActive(active);

        int layer = static_cast<int>(gameObject->GetLayer());
        if (ImGui::InputInt("Layer", &layer))
        {
            layer = (layer < 0) ? 0 : (layer > 31 ? 31 : layer);
            gameObject->SetLayer(static_cast<unsigned int>(layer));
        }

        ImGui::SeparatorText("Transform");
        if (Unity::CTransform* transform = SafeGetTransform(gameObject))
        {
            ImGui::InputFloat3("Local Position", state.editLocalPosition, "%.4f");
            ImGui::InputFloat3("Euler Rotation", state.editEuler, "%.4f");
            ImGui::InputFloat3("Local Scale", state.editLocalScale, "%.4f");

            if (AnimatedButton("Apply Transform"))
            {
                transform->SetLocalPosition(Unity::Vector3(
                    state.editLocalPosition[0],
                    state.editLocalPosition[1],
                    state.editLocalPosition[2]));

                Unity::Quaternion rotation{};
                rotation = rotation.Euler(
                    state.editEuler[0],
                    state.editEuler[1],
                    state.editEuler[2]);
                transform->SetRotation(rotation);

                transform->SetLocalScale(Unity::Vector3(
                    state.editLocalScale[0],
                    state.editLocalScale[1],
                    state.editLocalScale[2]));

                HBLog::Printf("[UExplorer] Applied transform to '%s'.\n", SafeGetObjectName(gameObject).c_str());
            }

            ImGui::SameLine();
            if (AnimatedButton("Reload Transform"))
                SyncTransformEditor(state, gameObject);
        }
        else
        {
            ImGui::TextDisabled("Transform API is not available for this object/version.");
        }

        ImGui::SeparatorText("Components");
        DrawComponentsInspector(state, gameObject);

        ImGui::End();
    }

    static bool LogLineMatchesFilter(const ExplorerState& state, const std::string& line)
    {
        if (state.logFilter[0] == '\0')
            return true;

        const std::string haystack = ToLowerCopy(line);
        const std::string needle = ToLowerCopy(std::string(state.logFilter));
        return haystack.find(needle) != std::string::npos;
    }

    static ImVec4 GetLogLineColor(const std::string& line)
    {
        const std::string lower = ToLowerCopy(line);
        if (lower.find("error") != std::string::npos || lower.find("exception") != std::string::npos)
            return ImVec4(0.95f, 0.45f, 0.45f, 1.0f);

        if (lower.find("warning") != std::string::npos || lower.find("warn") != std::string::npos)
            return ImVec4(0.98f, 0.78f, 0.42f, 1.0f);

        if (lower.find("[core]") != std::string::npos)
            return ImVec4(0.56f, 0.86f, 0.93f, 1.0f);

        if (lower.find("[uexplorer]") != std::string::npos)
            return ImVec4(0.62f, 0.90f, 0.72f, 1.0f);

        if (lower.find("[unity]") != std::string::npos)
            return ImVec4(0.86f, 0.86f, 0.90f, 1.0f);

        return ImGui::GetStyleColorVec4(ImGuiCol_Text);
    }

    static void DrawLogsWindow(ExplorerState& state)
    {
        std::vector<std::string> liveLines;
        HBLog::Snapshot(&liveLines);

        if (!ImGui::Begin("Logs", nullptr, ImGuiWindowFlags_NoCollapse))
        {
            ImGui::End();
            return;
        }

        if (AnimatedButton("Clear"))
        {
            HBLog::Clear();
            liveLines.clear();
        }
        ImGui::SameLine();
        ImGui::Checkbox("Auto-scroll", &state.logAutoScroll);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(280.0f);
        ImGui::InputText("Filter", state.logFilter, IM_ARRAYSIZE(state.logFilter));

        ImGui::Separator();
        ImGui::TextDisabled("Lines: %zu", liveLines.size());
        ImGui::Separator();

        if (ImGui::BeginChild("##LogsChild", ImVec2(0.0f, 0.0f), true, ImGuiWindowFlags_HorizontalScrollbar))
        {
            const bool wasAtBottom = (ImGui::GetScrollY() >= (ImGui::GetScrollMaxY() - 4.0f));
            const bool noFilter = (state.logFilter[0] == '\0');
            if (noFilter)
            {
                ImGuiListClipper clipper;
                clipper.Begin(static_cast<int>(liveLines.size()));
                while (clipper.Step())
                {
                    for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
                    {
                        const std::string& line = liveLines[static_cast<size_t>(i)];
                        ImGui::TextColored(GetLogLineColor(line), "%s", line.c_str());
                    }
                }
            }
            else
            {
                for (const std::string& line : liveLines)
                {
                    if (!LogLineMatchesFilter(state, line))
                        continue;

                    ImGui::TextColored(GetLogLineColor(line), "%s", line.c_str());
                }
            }

            if (state.logAutoScroll && wasAtBottom)
                ImGui::SetScrollHereY(1.0f);
        }
        ImGui::EndChild();

        ImGui::End();
    }

    static bool EnsureReady(ExplorerState& state)
    {
        if (!IL2CPP::Domain::Get())
        {
            if (!state.waitingLogPrinted)
            {
                HBLog::Printf("[UExplorer] Waiting for IL2CPP domain...\n");
                state.waitingLogPrinted = true;
            }
            return false;
        }

        if (!state.initialized)
        {
            ResolveRuntimeMethods(state);
            ForceRefresh(state);
            state.initialized = true;
            HBLog::Printf("[UExplorer] Initialized.\n");
        }

        return true;
    }

    void NotifyVisibilityChanged(bool visible)
    {
        ExplorerState& state = GetState();
        if (visible)
        {
            state.pendingRefresh = true;
            return;
        }

        state.selectedObject = nullptr;
        state.transformEditTarget = nullptr;
        state.navigationHistory.clear();
    }

    void Draw(bool* pOpen)
    {
        if (!pOpen || !(*pOpen))
            return;

        ExplorerState& state = GetState();
        if (!EnsureReady(state))
            return;

        if (state.pendingRefresh)
        {
            ForceRefresh(state);
            state.pendingRefresh = false;
        }

        TickRefresh(state);

        ImGuiViewport* viewport = ImGui::GetMainViewport();
        const ImVec2 origin = viewport->WorkPos;
        const ImVec2 size = viewport->WorkSize;
        const float panelTop = origin.y + 12.0f;
        const float panelHeight = size.y - 24.0f;
        const float rightPanelX = origin.x + size.x * 0.34f;
        const float rightPanelWidth = size.x * 0.64f;
        const float rightPanelSpacing = 8.0f;

        float logsHeight = panelHeight * 0.30f;
        if (logsHeight < 170.0f) logsHeight = 170.0f;
        if (logsHeight > panelHeight * 0.55f) logsHeight = panelHeight * 0.55f;
        float inspectorHeight = panelHeight - logsHeight - rightPanelSpacing;
        if (inspectorHeight < 220.0f)
        {
            inspectorHeight = 220.0f;
            logsHeight = panelHeight - inspectorHeight - rightPanelSpacing;
            if (logsHeight < 120.0f)
                logsHeight = 120.0f;
        }

        ImGui::SetNextWindowPos(ImVec2(origin.x + 12.0f, origin.y + 12.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(size.x * 0.32f, size.y - 24.0f), ImGuiCond_FirstUseEver);

        if (ImGui::Begin("Object Explorer", pOpen, ImGuiWindowFlags_NoCollapse))
        {
            if (ImGui::BeginTabBar("ExplorerTabs"))
            {
                if (ImGui::BeginTabItem("Scene Explorer"))
                {
                    DrawSceneExplorerTab(state);
                    ImGui::EndTabItem();
                }

                if (ImGui::BeginTabItem("Object Search"))
                {
                    DrawObjectSearchTab(state);
                    ImGui::EndTabItem();
                }

                ImGui::EndTabBar();
            }
        }
        ImGui::End();

        ImGui::SetNextWindowPos(ImVec2(rightPanelX, panelTop), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(rightPanelWidth, inspectorHeight), ImGuiCond_Always);
        DrawInspectorWindow(state);

        ImGui::SetNextWindowPos(ImVec2(rightPanelX, panelTop + inspectorHeight + rightPanelSpacing), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(rightPanelWidth, logsHeight), ImGuiCond_Always);
        DrawLogsWindow(state);
    }
}
