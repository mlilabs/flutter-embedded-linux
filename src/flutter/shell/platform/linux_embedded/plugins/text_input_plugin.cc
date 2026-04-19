// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/linux_embedded/plugins/text_input_plugin.h"

#include <linux/input-event-codes.h>

#include <iostream>

#include "flutter/shell/platform/common/json_method_codec.h"
#include "flutter/shell/platform/common/client_wrapper/include/flutter/standard_method_codec.h"

// Avoids the following build error:
// ----------------------------------------------------------------
//  error: expected unqualified-id
//    result->Success(document);
//            ^
// /usr/include/X11/X.h:350:21: note: expanded from macro 'Success'
// #define Success            0    /* everything's okay */
// ----------------------------------------------------------------
#if defined(DISPLAY_BACKEND_TYPE_X11)
#undef Success
#endif

namespace flutter {

namespace {
constexpr char kChannelName[] = "flutter/textinput";
constexpr char kELinuxChannelName[] = "elinux/textinput";
constexpr char kSetInputLocaleMethod[] = "setInputLocale";
constexpr char kSetVirtualKeyboardBottomInsetMethod[] =
    "setVirtualKeyboardBottomInset";

constexpr char kSetEditingStateMethod[] = "TextInput.setEditingState";
constexpr char kClearClientMethod[] = "TextInput.clearClient";
constexpr char kSetClientMethod[] = "TextInput.setClient";
constexpr char kShowMethod[] = "TextInput.show";
constexpr char kHideMethod[] = "TextInput.hide";

constexpr char kMultilineInputType[] = "TextInputType.multiline";

constexpr char kUpdateEditingStateMethod[] =
    "TextInputClient.updateEditingState";
constexpr char kPerformActionMethod[] = "TextInputClient.performAction";

constexpr char kTextInputAction[] = "inputAction";
constexpr char kTextInputType[] = "inputType";
constexpr char kTextInputTypeName[] = "name";
constexpr char kComposingBaseKey[] = "composingBase";
constexpr char kComposingExtentKey[] = "composingExtent";
constexpr char kSelectionAffinityKey[] = "selectionAffinity";
constexpr char kAffinityDownstream[] = "TextAffinity.downstream";
constexpr char kSelectionBaseKey[] = "selectionBase";
constexpr char kSelectionExtentKey[] = "selectionExtent";
constexpr char kSelectionIsDirectionalKey[] = "selectionIsDirectional";
constexpr char kTextKey[] = "text";
constexpr char kObscureTextKey[] = "obscureText";
constexpr char kAutocorrectKey[] = "autocorrect";
constexpr char kEnableSuggestionsKey[] = "enableSuggestions";
constexpr char kTextCapitalizationKey[] = "textCapitalization";

constexpr char kBadArgumentError[] = "Bad Arguments";
constexpr char kInternalConsistencyError[] = "Internal Consistency Error";
}  // namespace

void TextInputPlugin::OnKeyPressed(uint32_t keycode, uint32_t code_point) {
  if (!active_model_) {
    return;
  }

  bool changed = false;
  switch (keycode) {
    case KEY_LEFT:
      changed = active_model_->MoveCursorBack();
      break;
    case KEY_RIGHT:
      changed = active_model_->MoveCursorForward();
      break;
    case KEY_END:
      changed = active_model_->MoveCursorToEnd();
      break;
    case KEY_HOME:
      changed = active_model_->MoveCursorToBeginning();
      break;
    case KEY_BACKSPACE:
      changed = active_model_->Backspace();
      break;
    case KEY_DELETE:
      changed = active_model_->Delete();
      break;
    case KEY_ENTER:
      EnterPressed(active_model_.get());
      break;
    default:
      if (code_point) {
        active_model_->AddCodePoint(code_point);
        changed = true;
      }
      break;
  }
  if (changed) {
    SendStateUpdate(*active_model_);
  }
}

TextInputPlugin::TextInputPlugin(BinaryMessenger* messenger,
                                 WindowBindingHandler* delegate)
    : channel_(std::make_unique<flutter::MethodChannel<rapidjson::Document>>(
          messenger,
          kChannelName,
          &flutter::JsonMethodCodec::GetInstance())),
      elinux_channel_(
          std::make_unique<flutter::MethodChannel<flutter::EncodableValue>>(
              messenger,
              kELinuxChannelName,
              &flutter::StandardMethodCodec::GetInstance())),
      delegate_(delegate),
      active_model_(nullptr) {
  channel_->SetMethodCallHandler(
      [this](
          const flutter::MethodCall<rapidjson::Document>& call,
          std::unique_ptr<flutter::MethodResult<rapidjson::Document>> result) {
        HandleMethodCall(call, std::move(result));
      });
  elinux_channel_->SetMethodCallHandler(
      [this](
          const flutter::MethodCall<flutter::EncodableValue>& call,
          std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>>
              result) {
        const std::string& method = call.method_name();
        if (method == kSetInputLocaleMethod) {
          if (call.arguments() &&
              std::holds_alternative<std::string>(*call.arguments())) {
            input_locale_ = std::get<std::string>(*call.arguments());
          } else {
            input_locale_.clear();
          }
          result->Success();
        } else if (method == kSetVirtualKeyboardBottomInsetMethod) {
          if (!call.arguments()) {
            result->Error(kBadArgumentError, "Missing argument");
            return;
          }
          const auto& args = *call.arguments();
          int64_t height = 0;
          if (std::holds_alternative<int32_t>(args)) {
            height = std::get<int32_t>(args);
          } else if (std::holds_alternative<int64_t>(args)) {
            height = std::get<int64_t>(args);
          } else {
            result->Error(kBadArgumentError,
                          "Expected integer height in surface units");
            return;
          }
          if (height < 0) {
            result->Error(kBadArgumentError, "Height must be non-negative");
            return;
          }
          delegate_->SetVirtualKeyboardBottomInset(static_cast<int32_t>(height));
          result->Success();
        } else {
          result->NotImplemented();
        }
      });
}

void TextInputPlugin::HandleMethodCall(
    const flutter::MethodCall<rapidjson::Document>& method_call,
    std::unique_ptr<flutter::MethodResult<rapidjson::Document>> result) {
  const std::string& method = method_call.method_name();

  if (method.compare(kShowMethod) == 0) {
    TextInputTypeInfo info;
    info.input_type = input_type_;
    info.input_action = input_action_;
    info.obscure_text = obscure_text_;
    info.autocorrect = autocorrect_;
    info.enable_suggestions = enable_suggestions_;
    info.text_capitalization = text_capitalization_;
    info.preferred_language = input_locale_;
    delegate_->UpdateVirtualKeyboardStatus(true, info);
  } else if (method.compare(kHideMethod) == 0) {
    delegate_->UpdateVirtualKeyboardStatus(false);
  } else if (method.compare(kClearClientMethod) == 0) {
    active_model_ = nullptr;
  } else if (method.compare(kSetClientMethod) == 0) {
    if (!method_call.arguments() || method_call.arguments()->IsNull()) {
      result->Error(kBadArgumentError, "Method invoked without args");
      return;
    }
    const rapidjson::Document& args = *method_call.arguments();

    // TODO(awdavies): There's quite a wealth of arguments supplied with this
    // method, and they should be inspected/used.
    const rapidjson::Value& client_id_json = args[0];
    const rapidjson::Value& client_config = args[1];
    if (client_id_json.IsNull()) {
      result->Error(kBadArgumentError, "Could not set client, ID is null.");
      return;
    }
    if (client_config.IsNull()) {
      result->Error(kBadArgumentError,
                    "Could not set client, missing arguments.");
      return;
    }
    client_id_ = client_id_json.GetInt();
    input_action_ = "";
    auto input_action_json = client_config.FindMember(kTextInputAction);
    if (input_action_json != client_config.MemberEnd() &&
        input_action_json->value.IsString()) {
      input_action_ = input_action_json->value.GetString();
    }
    input_type_ = "";
    auto input_type_info_json = client_config.FindMember(kTextInputType);
    if (input_type_info_json != client_config.MemberEnd() &&
        input_type_info_json->value.IsObject()) {
      auto input_type_json =
          input_type_info_json->value.FindMember(kTextInputTypeName);
      if (input_type_json != input_type_info_json->value.MemberEnd() &&
          input_type_json->value.IsString()) {
        input_type_ = input_type_json->value.GetString();
      }
    }
    obscure_text_ = false;
    auto obscure_text_json = client_config.FindMember(kObscureTextKey);
    if (obscure_text_json != client_config.MemberEnd() &&
        obscure_text_json->value.IsBool()) {
      obscure_text_ = obscure_text_json->value.GetBool();
    }
    autocorrect_ = true;
    auto autocorrect_json = client_config.FindMember(kAutocorrectKey);
    if (autocorrect_json != client_config.MemberEnd() &&
        autocorrect_json->value.IsBool()) {
      autocorrect_ = autocorrect_json->value.GetBool();
    }
    enable_suggestions_ = true;
    auto enable_suggestions_json =
        client_config.FindMember(kEnableSuggestionsKey);
    if (enable_suggestions_json != client_config.MemberEnd() &&
        enable_suggestions_json->value.IsBool()) {
      enable_suggestions_ = enable_suggestions_json->value.GetBool();
    }
    text_capitalization_ = "";
    auto text_capitalization_json =
        client_config.FindMember(kTextCapitalizationKey);
    if (text_capitalization_json != client_config.MemberEnd() &&
        text_capitalization_json->value.IsString()) {
      text_capitalization_ = text_capitalization_json->value.GetString();
    }
    active_model_ = std::make_unique<TextInputModel>();
  } else if (method.compare(kSetEditingStateMethod) == 0) {
    if (!method_call.arguments() || method_call.arguments()->IsNull()) {
      result->Error(kBadArgumentError, "Method invoked without args");
      return;
    }
    const rapidjson::Document& args = *method_call.arguments();

    if (active_model_ == nullptr) {
      result->Error(
          kInternalConsistencyError,
          "Set editing state has been invoked, but no client is set.");
      return;
    }
    auto text = args.FindMember(kTextKey);
    if (text == args.MemberEnd() || text->value.IsNull()) {
      result->Error(kBadArgumentError,
                    "Set editing state has been invoked, but without text.");
      return;
    }
    auto selection_base = args.FindMember(kSelectionBaseKey);
    auto selection_extent = args.FindMember(kSelectionExtentKey);
    if (selection_base == args.MemberEnd() || selection_base->value.IsNull() ||
        selection_extent == args.MemberEnd() ||
        selection_extent->value.IsNull()) {
      result->Error(kInternalConsistencyError,
                    "Selection base/extent values invalid.");
      return;
    }
    // Flutter uses -1/-1 for invalid; translate that to 0/0 for the model.
    int base = selection_base->value.GetInt();
    int extent = selection_extent->value.GetInt();
    if (base == -1 && extent == -1) {
      base = extent = 0;
    }
    active_model_->SetText(text->value.GetString());
    active_model_->SetSelection(TextRange(base, extent));
  } else {
    result->NotImplemented();
    return;
  }
  // All error conditions return early, so if nothing has gone wrong indicate
  // success.
  result->Success();
}

void TextInputPlugin::SendStateUpdate(const TextInputModel& model) {
  auto args = std::make_unique<rapidjson::Document>(rapidjson::kArrayType);
  auto& allocator = args->GetAllocator();
  args->PushBack(client_id_, allocator);

  TextRange selection = model.selection();
  rapidjson::Value editing_state(rapidjson::kObjectType);
  editing_state.AddMember(kComposingBaseKey, -1, allocator);
  editing_state.AddMember(kComposingExtentKey, -1, allocator);
  editing_state.AddMember(kSelectionAffinityKey, kAffinityDownstream,
                          allocator);
  editing_state.AddMember(kSelectionBaseKey, selection.base(), allocator);
  editing_state.AddMember(kSelectionExtentKey, selection.extent(), allocator);
  editing_state.AddMember(kSelectionIsDirectionalKey, false, allocator);
  editing_state.AddMember(
      kTextKey, rapidjson::Value(model.GetText(), allocator).Move(), allocator);
  args->PushBack(editing_state, allocator);

  channel_->InvokeMethod(kUpdateEditingStateMethod, std::move(args));
}

void TextInputPlugin::EnterPressed(TextInputModel* model) {
  if (input_type_ == kMultilineInputType) {
    model->AddCodePoint('\n');
    SendStateUpdate(*model);
  }
  auto args = std::make_unique<rapidjson::Document>(rapidjson::kArrayType);
  auto& allocator = args->GetAllocator();
  args->PushBack(client_id_, allocator);
  args->PushBack(rapidjson::Value(input_action_, allocator).Move(), allocator);

  channel_->InvokeMethod(kPerformActionMethod, std::move(args));
}

}  // namespace flutter
