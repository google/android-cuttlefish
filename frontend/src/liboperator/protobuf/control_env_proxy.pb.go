// Copyright (C) 2023 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// Please execute src/protocutil in public github when updating this file.

// Code generated by protoc-gen-go. DO NOT EDIT.
// versions:
// 	protoc-gen-go v1.31.0
// 	protoc        v3.21.12
// source: control_env_proxy.proto

package protobuf

import (
	protoreflect "google.golang.org/protobuf/reflect/protoreflect"
	protoimpl "google.golang.org/protobuf/runtime/protoimpl"
	emptypb "google.golang.org/protobuf/types/known/emptypb"
	reflect "reflect"
	sync "sync"
)

const (
	// Verify that this generated code is sufficiently up-to-date.
	_ = protoimpl.EnforceVersion(20 - protoimpl.MinVersion)
	// Verify that runtime/protoimpl is sufficiently up-to-date.
	_ = protoimpl.EnforceVersion(protoimpl.MaxVersion - 20)
)

type CallUnaryMethodRequest struct {
	state         protoimpl.MessageState
	sizeCache     protoimpl.SizeCache
	unknownFields protoimpl.UnknownFields

	ServiceName        string `protobuf:"bytes,1,opt,name=service_name,json=serviceName,proto3" json:"service_name,omitempty"`
	MethodName         string `protobuf:"bytes,2,opt,name=method_name,json=methodName,proto3" json:"method_name,omitempty"`
	JsonFormattedProto string `protobuf:"bytes,3,opt,name=json_formatted_proto,json=jsonFormattedProto,proto3" json:"json_formatted_proto,omitempty"`
}

func (x *CallUnaryMethodRequest) Reset() {
	*x = CallUnaryMethodRequest{}
	if protoimpl.UnsafeEnabled {
		mi := &file_control_env_proxy_proto_msgTypes[0]
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		ms.StoreMessageInfo(mi)
	}
}

func (x *CallUnaryMethodRequest) String() string {
	return protoimpl.X.MessageStringOf(x)
}

func (*CallUnaryMethodRequest) ProtoMessage() {}

func (x *CallUnaryMethodRequest) ProtoReflect() protoreflect.Message {
	mi := &file_control_env_proxy_proto_msgTypes[0]
	if protoimpl.UnsafeEnabled && x != nil {
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		if ms.LoadMessageInfo() == nil {
			ms.StoreMessageInfo(mi)
		}
		return ms
	}
	return mi.MessageOf(x)
}

// Deprecated: Use CallUnaryMethodRequest.ProtoReflect.Descriptor instead.
func (*CallUnaryMethodRequest) Descriptor() ([]byte, []int) {
	return file_control_env_proxy_proto_rawDescGZIP(), []int{0}
}

func (x *CallUnaryMethodRequest) GetServiceName() string {
	if x != nil {
		return x.ServiceName
	}
	return ""
}

func (x *CallUnaryMethodRequest) GetMethodName() string {
	if x != nil {
		return x.MethodName
	}
	return ""
}

func (x *CallUnaryMethodRequest) GetJsonFormattedProto() string {
	if x != nil {
		return x.JsonFormattedProto
	}
	return ""
}

type CallUnaryMethodReply struct {
	state         protoimpl.MessageState
	sizeCache     protoimpl.SizeCache
	unknownFields protoimpl.UnknownFields

	JsonFormattedProto string `protobuf:"bytes,1,opt,name=json_formatted_proto,json=jsonFormattedProto,proto3" json:"json_formatted_proto,omitempty"`
}

func (x *CallUnaryMethodReply) Reset() {
	*x = CallUnaryMethodReply{}
	if protoimpl.UnsafeEnabled {
		mi := &file_control_env_proxy_proto_msgTypes[1]
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		ms.StoreMessageInfo(mi)
	}
}

func (x *CallUnaryMethodReply) String() string {
	return protoimpl.X.MessageStringOf(x)
}

func (*CallUnaryMethodReply) ProtoMessage() {}

func (x *CallUnaryMethodReply) ProtoReflect() protoreflect.Message {
	mi := &file_control_env_proxy_proto_msgTypes[1]
	if protoimpl.UnsafeEnabled && x != nil {
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		if ms.LoadMessageInfo() == nil {
			ms.StoreMessageInfo(mi)
		}
		return ms
	}
	return mi.MessageOf(x)
}

// Deprecated: Use CallUnaryMethodReply.ProtoReflect.Descriptor instead.
func (*CallUnaryMethodReply) Descriptor() ([]byte, []int) {
	return file_control_env_proxy_proto_rawDescGZIP(), []int{1}
}

func (x *CallUnaryMethodReply) GetJsonFormattedProto() string {
	if x != nil {
		return x.JsonFormattedProto
	}
	return ""
}

type ListServicesReply struct {
	state         protoimpl.MessageState
	sizeCache     protoimpl.SizeCache
	unknownFields protoimpl.UnknownFields

	Services []string `protobuf:"bytes,1,rep,name=services,proto3" json:"services,omitempty"`
}

func (x *ListServicesReply) Reset() {
	*x = ListServicesReply{}
	if protoimpl.UnsafeEnabled {
		mi := &file_control_env_proxy_proto_msgTypes[2]
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		ms.StoreMessageInfo(mi)
	}
}

func (x *ListServicesReply) String() string {
	return protoimpl.X.MessageStringOf(x)
}

func (*ListServicesReply) ProtoMessage() {}

func (x *ListServicesReply) ProtoReflect() protoreflect.Message {
	mi := &file_control_env_proxy_proto_msgTypes[2]
	if protoimpl.UnsafeEnabled && x != nil {
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		if ms.LoadMessageInfo() == nil {
			ms.StoreMessageInfo(mi)
		}
		return ms
	}
	return mi.MessageOf(x)
}

// Deprecated: Use ListServicesReply.ProtoReflect.Descriptor instead.
func (*ListServicesReply) Descriptor() ([]byte, []int) {
	return file_control_env_proxy_proto_rawDescGZIP(), []int{2}
}

func (x *ListServicesReply) GetServices() []string {
	if x != nil {
		return x.Services
	}
	return nil
}

type ListMethodsRequest struct {
	state         protoimpl.MessageState
	sizeCache     protoimpl.SizeCache
	unknownFields protoimpl.UnknownFields

	ServiceName string `protobuf:"bytes,1,opt,name=service_name,json=serviceName,proto3" json:"service_name,omitempty"`
}

func (x *ListMethodsRequest) Reset() {
	*x = ListMethodsRequest{}
	if protoimpl.UnsafeEnabled {
		mi := &file_control_env_proxy_proto_msgTypes[3]
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		ms.StoreMessageInfo(mi)
	}
}

func (x *ListMethodsRequest) String() string {
	return protoimpl.X.MessageStringOf(x)
}

func (*ListMethodsRequest) ProtoMessage() {}

func (x *ListMethodsRequest) ProtoReflect() protoreflect.Message {
	mi := &file_control_env_proxy_proto_msgTypes[3]
	if protoimpl.UnsafeEnabled && x != nil {
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		if ms.LoadMessageInfo() == nil {
			ms.StoreMessageInfo(mi)
		}
		return ms
	}
	return mi.MessageOf(x)
}

// Deprecated: Use ListMethodsRequest.ProtoReflect.Descriptor instead.
func (*ListMethodsRequest) Descriptor() ([]byte, []int) {
	return file_control_env_proxy_proto_rawDescGZIP(), []int{3}
}

func (x *ListMethodsRequest) GetServiceName() string {
	if x != nil {
		return x.ServiceName
	}
	return ""
}

type ListMethodsReply struct {
	state         protoimpl.MessageState
	sizeCache     protoimpl.SizeCache
	unknownFields protoimpl.UnknownFields

	Methods []string `protobuf:"bytes,1,rep,name=methods,proto3" json:"methods,omitempty"`
}

func (x *ListMethodsReply) Reset() {
	*x = ListMethodsReply{}
	if protoimpl.UnsafeEnabled {
		mi := &file_control_env_proxy_proto_msgTypes[4]
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		ms.StoreMessageInfo(mi)
	}
}

func (x *ListMethodsReply) String() string {
	return protoimpl.X.MessageStringOf(x)
}

func (*ListMethodsReply) ProtoMessage() {}

func (x *ListMethodsReply) ProtoReflect() protoreflect.Message {
	mi := &file_control_env_proxy_proto_msgTypes[4]
	if protoimpl.UnsafeEnabled && x != nil {
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		if ms.LoadMessageInfo() == nil {
			ms.StoreMessageInfo(mi)
		}
		return ms
	}
	return mi.MessageOf(x)
}

// Deprecated: Use ListMethodsReply.ProtoReflect.Descriptor instead.
func (*ListMethodsReply) Descriptor() ([]byte, []int) {
	return file_control_env_proxy_proto_rawDescGZIP(), []int{4}
}

func (x *ListMethodsReply) GetMethods() []string {
	if x != nil {
		return x.Methods
	}
	return nil
}

type ListReqResTypeRequest struct {
	state         protoimpl.MessageState
	sizeCache     protoimpl.SizeCache
	unknownFields protoimpl.UnknownFields

	ServiceName string `protobuf:"bytes,1,opt,name=service_name,json=serviceName,proto3" json:"service_name,omitempty"`
	MethodName  string `protobuf:"bytes,2,opt,name=method_name,json=methodName,proto3" json:"method_name,omitempty"`
}

func (x *ListReqResTypeRequest) Reset() {
	*x = ListReqResTypeRequest{}
	if protoimpl.UnsafeEnabled {
		mi := &file_control_env_proxy_proto_msgTypes[5]
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		ms.StoreMessageInfo(mi)
	}
}

func (x *ListReqResTypeRequest) String() string {
	return protoimpl.X.MessageStringOf(x)
}

func (*ListReqResTypeRequest) ProtoMessage() {}

func (x *ListReqResTypeRequest) ProtoReflect() protoreflect.Message {
	mi := &file_control_env_proxy_proto_msgTypes[5]
	if protoimpl.UnsafeEnabled && x != nil {
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		if ms.LoadMessageInfo() == nil {
			ms.StoreMessageInfo(mi)
		}
		return ms
	}
	return mi.MessageOf(x)
}

// Deprecated: Use ListReqResTypeRequest.ProtoReflect.Descriptor instead.
func (*ListReqResTypeRequest) Descriptor() ([]byte, []int) {
	return file_control_env_proxy_proto_rawDescGZIP(), []int{5}
}

func (x *ListReqResTypeRequest) GetServiceName() string {
	if x != nil {
		return x.ServiceName
	}
	return ""
}

func (x *ListReqResTypeRequest) GetMethodName() string {
	if x != nil {
		return x.MethodName
	}
	return ""
}

type ListReqResTypeReply struct {
	state         protoimpl.MessageState
	sizeCache     protoimpl.SizeCache
	unknownFields protoimpl.UnknownFields

	RequestTypeName  string `protobuf:"bytes,1,opt,name=request_type_name,json=requestTypeName,proto3" json:"request_type_name,omitempty"`
	ResponseTypeName string `protobuf:"bytes,2,opt,name=response_type_name,json=responseTypeName,proto3" json:"response_type_name,omitempty"`
}

func (x *ListReqResTypeReply) Reset() {
	*x = ListReqResTypeReply{}
	if protoimpl.UnsafeEnabled {
		mi := &file_control_env_proxy_proto_msgTypes[6]
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		ms.StoreMessageInfo(mi)
	}
}

func (x *ListReqResTypeReply) String() string {
	return protoimpl.X.MessageStringOf(x)
}

func (*ListReqResTypeReply) ProtoMessage() {}

func (x *ListReqResTypeReply) ProtoReflect() protoreflect.Message {
	mi := &file_control_env_proxy_proto_msgTypes[6]
	if protoimpl.UnsafeEnabled && x != nil {
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		if ms.LoadMessageInfo() == nil {
			ms.StoreMessageInfo(mi)
		}
		return ms
	}
	return mi.MessageOf(x)
}

// Deprecated: Use ListReqResTypeReply.ProtoReflect.Descriptor instead.
func (*ListReqResTypeReply) Descriptor() ([]byte, []int) {
	return file_control_env_proxy_proto_rawDescGZIP(), []int{6}
}

func (x *ListReqResTypeReply) GetRequestTypeName() string {
	if x != nil {
		return x.RequestTypeName
	}
	return ""
}

func (x *ListReqResTypeReply) GetResponseTypeName() string {
	if x != nil {
		return x.ResponseTypeName
	}
	return ""
}

var File_control_env_proxy_proto protoreflect.FileDescriptor

var file_control_env_proxy_proto_rawDesc = []byte{
	0x0a, 0x17, 0x63, 0x6f, 0x6e, 0x74, 0x72, 0x6f, 0x6c, 0x5f, 0x65, 0x6e, 0x76, 0x5f, 0x70, 0x72,
	0x6f, 0x78, 0x79, 0x2e, 0x70, 0x72, 0x6f, 0x74, 0x6f, 0x12, 0x15, 0x63, 0x6f, 0x6e, 0x74, 0x72,
	0x6f, 0x6c, 0x65, 0x6e, 0x76, 0x70, 0x72, 0x6f, 0x78, 0x79, 0x73, 0x65, 0x72, 0x76, 0x65, 0x72,
	0x1a, 0x1b, 0x67, 0x6f, 0x6f, 0x67, 0x6c, 0x65, 0x2f, 0x70, 0x72, 0x6f, 0x74, 0x6f, 0x62, 0x75,
	0x66, 0x2f, 0x65, 0x6d, 0x70, 0x74, 0x79, 0x2e, 0x70, 0x72, 0x6f, 0x74, 0x6f, 0x22, 0x8e, 0x01,
	0x0a, 0x16, 0x43, 0x61, 0x6c, 0x6c, 0x55, 0x6e, 0x61, 0x72, 0x79, 0x4d, 0x65, 0x74, 0x68, 0x6f,
	0x64, 0x52, 0x65, 0x71, 0x75, 0x65, 0x73, 0x74, 0x12, 0x21, 0x0a, 0x0c, 0x73, 0x65, 0x72, 0x76,
	0x69, 0x63, 0x65, 0x5f, 0x6e, 0x61, 0x6d, 0x65, 0x18, 0x01, 0x20, 0x01, 0x28, 0x09, 0x52, 0x0b,
	0x73, 0x65, 0x72, 0x76, 0x69, 0x63, 0x65, 0x4e, 0x61, 0x6d, 0x65, 0x12, 0x1f, 0x0a, 0x0b, 0x6d,
	0x65, 0x74, 0x68, 0x6f, 0x64, 0x5f, 0x6e, 0x61, 0x6d, 0x65, 0x18, 0x02, 0x20, 0x01, 0x28, 0x09,
	0x52, 0x0a, 0x6d, 0x65, 0x74, 0x68, 0x6f, 0x64, 0x4e, 0x61, 0x6d, 0x65, 0x12, 0x30, 0x0a, 0x14,
	0x6a, 0x73, 0x6f, 0x6e, 0x5f, 0x66, 0x6f, 0x72, 0x6d, 0x61, 0x74, 0x74, 0x65, 0x64, 0x5f, 0x70,
	0x72, 0x6f, 0x74, 0x6f, 0x18, 0x03, 0x20, 0x01, 0x28, 0x09, 0x52, 0x12, 0x6a, 0x73, 0x6f, 0x6e,
	0x46, 0x6f, 0x72, 0x6d, 0x61, 0x74, 0x74, 0x65, 0x64, 0x50, 0x72, 0x6f, 0x74, 0x6f, 0x22, 0x48,
	0x0a, 0x14, 0x43, 0x61, 0x6c, 0x6c, 0x55, 0x6e, 0x61, 0x72, 0x79, 0x4d, 0x65, 0x74, 0x68, 0x6f,
	0x64, 0x52, 0x65, 0x70, 0x6c, 0x79, 0x12, 0x30, 0x0a, 0x14, 0x6a, 0x73, 0x6f, 0x6e, 0x5f, 0x66,
	0x6f, 0x72, 0x6d, 0x61, 0x74, 0x74, 0x65, 0x64, 0x5f, 0x70, 0x72, 0x6f, 0x74, 0x6f, 0x18, 0x01,
	0x20, 0x01, 0x28, 0x09, 0x52, 0x12, 0x6a, 0x73, 0x6f, 0x6e, 0x46, 0x6f, 0x72, 0x6d, 0x61, 0x74,
	0x74, 0x65, 0x64, 0x50, 0x72, 0x6f, 0x74, 0x6f, 0x22, 0x2f, 0x0a, 0x11, 0x4c, 0x69, 0x73, 0x74,
	0x53, 0x65, 0x72, 0x76, 0x69, 0x63, 0x65, 0x73, 0x52, 0x65, 0x70, 0x6c, 0x79, 0x12, 0x1a, 0x0a,
	0x08, 0x73, 0x65, 0x72, 0x76, 0x69, 0x63, 0x65, 0x73, 0x18, 0x01, 0x20, 0x03, 0x28, 0x09, 0x52,
	0x08, 0x73, 0x65, 0x72, 0x76, 0x69, 0x63, 0x65, 0x73, 0x22, 0x37, 0x0a, 0x12, 0x4c, 0x69, 0x73,
	0x74, 0x4d, 0x65, 0x74, 0x68, 0x6f, 0x64, 0x73, 0x52, 0x65, 0x71, 0x75, 0x65, 0x73, 0x74, 0x12,
	0x21, 0x0a, 0x0c, 0x73, 0x65, 0x72, 0x76, 0x69, 0x63, 0x65, 0x5f, 0x6e, 0x61, 0x6d, 0x65, 0x18,
	0x01, 0x20, 0x01, 0x28, 0x09, 0x52, 0x0b, 0x73, 0x65, 0x72, 0x76, 0x69, 0x63, 0x65, 0x4e, 0x61,
	0x6d, 0x65, 0x22, 0x2c, 0x0a, 0x10, 0x4c, 0x69, 0x73, 0x74, 0x4d, 0x65, 0x74, 0x68, 0x6f, 0x64,
	0x73, 0x52, 0x65, 0x70, 0x6c, 0x79, 0x12, 0x18, 0x0a, 0x07, 0x6d, 0x65, 0x74, 0x68, 0x6f, 0x64,
	0x73, 0x18, 0x01, 0x20, 0x03, 0x28, 0x09, 0x52, 0x07, 0x6d, 0x65, 0x74, 0x68, 0x6f, 0x64, 0x73,
	0x22, 0x5b, 0x0a, 0x15, 0x4c, 0x69, 0x73, 0x74, 0x52, 0x65, 0x71, 0x52, 0x65, 0x73, 0x54, 0x79,
	0x70, 0x65, 0x52, 0x65, 0x71, 0x75, 0x65, 0x73, 0x74, 0x12, 0x21, 0x0a, 0x0c, 0x73, 0x65, 0x72,
	0x76, 0x69, 0x63, 0x65, 0x5f, 0x6e, 0x61, 0x6d, 0x65, 0x18, 0x01, 0x20, 0x01, 0x28, 0x09, 0x52,
	0x0b, 0x73, 0x65, 0x72, 0x76, 0x69, 0x63, 0x65, 0x4e, 0x61, 0x6d, 0x65, 0x12, 0x1f, 0x0a, 0x0b,
	0x6d, 0x65, 0x74, 0x68, 0x6f, 0x64, 0x5f, 0x6e, 0x61, 0x6d, 0x65, 0x18, 0x02, 0x20, 0x01, 0x28,
	0x09, 0x52, 0x0a, 0x6d, 0x65, 0x74, 0x68, 0x6f, 0x64, 0x4e, 0x61, 0x6d, 0x65, 0x22, 0x6f, 0x0a,
	0x13, 0x4c, 0x69, 0x73, 0x74, 0x52, 0x65, 0x71, 0x52, 0x65, 0x73, 0x54, 0x79, 0x70, 0x65, 0x52,
	0x65, 0x70, 0x6c, 0x79, 0x12, 0x2a, 0x0a, 0x11, 0x72, 0x65, 0x71, 0x75, 0x65, 0x73, 0x74, 0x5f,
	0x74, 0x79, 0x70, 0x65, 0x5f, 0x6e, 0x61, 0x6d, 0x65, 0x18, 0x01, 0x20, 0x01, 0x28, 0x09, 0x52,
	0x0f, 0x72, 0x65, 0x71, 0x75, 0x65, 0x73, 0x74, 0x54, 0x79, 0x70, 0x65, 0x4e, 0x61, 0x6d, 0x65,
	0x12, 0x2c, 0x0a, 0x12, 0x72, 0x65, 0x73, 0x70, 0x6f, 0x6e, 0x73, 0x65, 0x5f, 0x74, 0x79, 0x70,
	0x65, 0x5f, 0x6e, 0x61, 0x6d, 0x65, 0x18, 0x02, 0x20, 0x01, 0x28, 0x09, 0x52, 0x10, 0x72, 0x65,
	0x73, 0x70, 0x6f, 0x6e, 0x73, 0x65, 0x54, 0x79, 0x70, 0x65, 0x4e, 0x61, 0x6d, 0x65, 0x32, 0xb0,
	0x03, 0x0a, 0x16, 0x43, 0x6f, 0x6e, 0x74, 0x72, 0x6f, 0x6c, 0x45, 0x6e, 0x76, 0x50, 0x72, 0x6f,
	0x78, 0x79, 0x53, 0x65, 0x72, 0x76, 0x69, 0x63, 0x65, 0x12, 0x6f, 0x0a, 0x0f, 0x43, 0x61, 0x6c,
	0x6c, 0x55, 0x6e, 0x61, 0x72, 0x79, 0x4d, 0x65, 0x74, 0x68, 0x6f, 0x64, 0x12, 0x2d, 0x2e, 0x63,
	0x6f, 0x6e, 0x74, 0x72, 0x6f, 0x6c, 0x65, 0x6e, 0x76, 0x70, 0x72, 0x6f, 0x78, 0x79, 0x73, 0x65,
	0x72, 0x76, 0x65, 0x72, 0x2e, 0x43, 0x61, 0x6c, 0x6c, 0x55, 0x6e, 0x61, 0x72, 0x79, 0x4d, 0x65,
	0x74, 0x68, 0x6f, 0x64, 0x52, 0x65, 0x71, 0x75, 0x65, 0x73, 0x74, 0x1a, 0x2b, 0x2e, 0x63, 0x6f,
	0x6e, 0x74, 0x72, 0x6f, 0x6c, 0x65, 0x6e, 0x76, 0x70, 0x72, 0x6f, 0x78, 0x79, 0x73, 0x65, 0x72,
	0x76, 0x65, 0x72, 0x2e, 0x43, 0x61, 0x6c, 0x6c, 0x55, 0x6e, 0x61, 0x72, 0x79, 0x4d, 0x65, 0x74,
	0x68, 0x6f, 0x64, 0x52, 0x65, 0x70, 0x6c, 0x79, 0x22, 0x00, 0x12, 0x52, 0x0a, 0x0c, 0x4c, 0x69,
	0x73, 0x74, 0x53, 0x65, 0x72, 0x76, 0x69, 0x63, 0x65, 0x73, 0x12, 0x16, 0x2e, 0x67, 0x6f, 0x6f,
	0x67, 0x6c, 0x65, 0x2e, 0x70, 0x72, 0x6f, 0x74, 0x6f, 0x62, 0x75, 0x66, 0x2e, 0x45, 0x6d, 0x70,
	0x74, 0x79, 0x1a, 0x28, 0x2e, 0x63, 0x6f, 0x6e, 0x74, 0x72, 0x6f, 0x6c, 0x65, 0x6e, 0x76, 0x70,
	0x72, 0x6f, 0x78, 0x79, 0x73, 0x65, 0x72, 0x76, 0x65, 0x72, 0x2e, 0x4c, 0x69, 0x73, 0x74, 0x53,
	0x65, 0x72, 0x76, 0x69, 0x63, 0x65, 0x73, 0x52, 0x65, 0x70, 0x6c, 0x79, 0x22, 0x00, 0x12, 0x63,
	0x0a, 0x0b, 0x4c, 0x69, 0x73, 0x74, 0x4d, 0x65, 0x74, 0x68, 0x6f, 0x64, 0x73, 0x12, 0x29, 0x2e,
	0x63, 0x6f, 0x6e, 0x74, 0x72, 0x6f, 0x6c, 0x65, 0x6e, 0x76, 0x70, 0x72, 0x6f, 0x78, 0x79, 0x73,
	0x65, 0x72, 0x76, 0x65, 0x72, 0x2e, 0x4c, 0x69, 0x73, 0x74, 0x4d, 0x65, 0x74, 0x68, 0x6f, 0x64,
	0x73, 0x52, 0x65, 0x71, 0x75, 0x65, 0x73, 0x74, 0x1a, 0x27, 0x2e, 0x63, 0x6f, 0x6e, 0x74, 0x72,
	0x6f, 0x6c, 0x65, 0x6e, 0x76, 0x70, 0x72, 0x6f, 0x78, 0x79, 0x73, 0x65, 0x72, 0x76, 0x65, 0x72,
	0x2e, 0x4c, 0x69, 0x73, 0x74, 0x4d, 0x65, 0x74, 0x68, 0x6f, 0x64, 0x73, 0x52, 0x65, 0x70, 0x6c,
	0x79, 0x22, 0x00, 0x12, 0x6c, 0x0a, 0x0e, 0x4c, 0x69, 0x73, 0x74, 0x52, 0x65, 0x71, 0x52, 0x65,
	0x73, 0x54, 0x79, 0x70, 0x65, 0x12, 0x2c, 0x2e, 0x63, 0x6f, 0x6e, 0x74, 0x72, 0x6f, 0x6c, 0x65,
	0x6e, 0x76, 0x70, 0x72, 0x6f, 0x78, 0x79, 0x73, 0x65, 0x72, 0x76, 0x65, 0x72, 0x2e, 0x4c, 0x69,
	0x73, 0x74, 0x52, 0x65, 0x71, 0x52, 0x65, 0x73, 0x54, 0x79, 0x70, 0x65, 0x52, 0x65, 0x71, 0x75,
	0x65, 0x73, 0x74, 0x1a, 0x2a, 0x2e, 0x63, 0x6f, 0x6e, 0x74, 0x72, 0x6f, 0x6c, 0x65, 0x6e, 0x76,
	0x70, 0x72, 0x6f, 0x78, 0x79, 0x73, 0x65, 0x72, 0x76, 0x65, 0x72, 0x2e, 0x4c, 0x69, 0x73, 0x74,
	0x52, 0x65, 0x71, 0x52, 0x65, 0x73, 0x54, 0x79, 0x70, 0x65, 0x52, 0x65, 0x70, 0x6c, 0x79, 0x22,
	0x00, 0x42, 0x16, 0x5a, 0x14, 0x6c, 0x69, 0x62, 0x6f, 0x70, 0x65, 0x72, 0x61, 0x74, 0x6f, 0x72,
	0x2f, 0x70, 0x72, 0x6f, 0x74, 0x6f, 0x62, 0x75, 0x66, 0x62, 0x06, 0x70, 0x72, 0x6f, 0x74, 0x6f,
	0x33,
}

var (
	file_control_env_proxy_proto_rawDescOnce sync.Once
	file_control_env_proxy_proto_rawDescData = file_control_env_proxy_proto_rawDesc
)

func file_control_env_proxy_proto_rawDescGZIP() []byte {
	file_control_env_proxy_proto_rawDescOnce.Do(func() {
		file_control_env_proxy_proto_rawDescData = protoimpl.X.CompressGZIP(file_control_env_proxy_proto_rawDescData)
	})
	return file_control_env_proxy_proto_rawDescData
}

var file_control_env_proxy_proto_msgTypes = make([]protoimpl.MessageInfo, 7)
var file_control_env_proxy_proto_goTypes = []interface{}{
	(*CallUnaryMethodRequest)(nil), // 0: controlenvproxyserver.CallUnaryMethodRequest
	(*CallUnaryMethodReply)(nil),   // 1: controlenvproxyserver.CallUnaryMethodReply
	(*ListServicesReply)(nil),      // 2: controlenvproxyserver.ListServicesReply
	(*ListMethodsRequest)(nil),     // 3: controlenvproxyserver.ListMethodsRequest
	(*ListMethodsReply)(nil),       // 4: controlenvproxyserver.ListMethodsReply
	(*ListReqResTypeRequest)(nil),  // 5: controlenvproxyserver.ListReqResTypeRequest
	(*ListReqResTypeReply)(nil),    // 6: controlenvproxyserver.ListReqResTypeReply
	(*emptypb.Empty)(nil),          // 7: google.protobuf.Empty
}
var file_control_env_proxy_proto_depIdxs = []int32{
	0, // 0: controlenvproxyserver.ControlEnvProxyService.CallUnaryMethod:input_type -> controlenvproxyserver.CallUnaryMethodRequest
	7, // 1: controlenvproxyserver.ControlEnvProxyService.ListServices:input_type -> google.protobuf.Empty
	3, // 2: controlenvproxyserver.ControlEnvProxyService.ListMethods:input_type -> controlenvproxyserver.ListMethodsRequest
	5, // 3: controlenvproxyserver.ControlEnvProxyService.ListReqResType:input_type -> controlenvproxyserver.ListReqResTypeRequest
	1, // 4: controlenvproxyserver.ControlEnvProxyService.CallUnaryMethod:output_type -> controlenvproxyserver.CallUnaryMethodReply
	2, // 5: controlenvproxyserver.ControlEnvProxyService.ListServices:output_type -> controlenvproxyserver.ListServicesReply
	4, // 6: controlenvproxyserver.ControlEnvProxyService.ListMethods:output_type -> controlenvproxyserver.ListMethodsReply
	6, // 7: controlenvproxyserver.ControlEnvProxyService.ListReqResType:output_type -> controlenvproxyserver.ListReqResTypeReply
	4, // [4:8] is the sub-list for method output_type
	0, // [0:4] is the sub-list for method input_type
	0, // [0:0] is the sub-list for extension type_name
	0, // [0:0] is the sub-list for extension extendee
	0, // [0:0] is the sub-list for field type_name
}

func init() { file_control_env_proxy_proto_init() }
func file_control_env_proxy_proto_init() {
	if File_control_env_proxy_proto != nil {
		return
	}
	if !protoimpl.UnsafeEnabled {
		file_control_env_proxy_proto_msgTypes[0].Exporter = func(v interface{}, i int) interface{} {
			switch v := v.(*CallUnaryMethodRequest); i {
			case 0:
				return &v.state
			case 1:
				return &v.sizeCache
			case 2:
				return &v.unknownFields
			default:
				return nil
			}
		}
		file_control_env_proxy_proto_msgTypes[1].Exporter = func(v interface{}, i int) interface{} {
			switch v := v.(*CallUnaryMethodReply); i {
			case 0:
				return &v.state
			case 1:
				return &v.sizeCache
			case 2:
				return &v.unknownFields
			default:
				return nil
			}
		}
		file_control_env_proxy_proto_msgTypes[2].Exporter = func(v interface{}, i int) interface{} {
			switch v := v.(*ListServicesReply); i {
			case 0:
				return &v.state
			case 1:
				return &v.sizeCache
			case 2:
				return &v.unknownFields
			default:
				return nil
			}
		}
		file_control_env_proxy_proto_msgTypes[3].Exporter = func(v interface{}, i int) interface{} {
			switch v := v.(*ListMethodsRequest); i {
			case 0:
				return &v.state
			case 1:
				return &v.sizeCache
			case 2:
				return &v.unknownFields
			default:
				return nil
			}
		}
		file_control_env_proxy_proto_msgTypes[4].Exporter = func(v interface{}, i int) interface{} {
			switch v := v.(*ListMethodsReply); i {
			case 0:
				return &v.state
			case 1:
				return &v.sizeCache
			case 2:
				return &v.unknownFields
			default:
				return nil
			}
		}
		file_control_env_proxy_proto_msgTypes[5].Exporter = func(v interface{}, i int) interface{} {
			switch v := v.(*ListReqResTypeRequest); i {
			case 0:
				return &v.state
			case 1:
				return &v.sizeCache
			case 2:
				return &v.unknownFields
			default:
				return nil
			}
		}
		file_control_env_proxy_proto_msgTypes[6].Exporter = func(v interface{}, i int) interface{} {
			switch v := v.(*ListReqResTypeReply); i {
			case 0:
				return &v.state
			case 1:
				return &v.sizeCache
			case 2:
				return &v.unknownFields
			default:
				return nil
			}
		}
	}
	type x struct{}
	out := protoimpl.TypeBuilder{
		File: protoimpl.DescBuilder{
			GoPackagePath: reflect.TypeOf(x{}).PkgPath(),
			RawDescriptor: file_control_env_proxy_proto_rawDesc,
			NumEnums:      0,
			NumMessages:   7,
			NumExtensions: 0,
			NumServices:   1,
		},
		GoTypes:           file_control_env_proxy_proto_goTypes,
		DependencyIndexes: file_control_env_proxy_proto_depIdxs,
		MessageInfos:      file_control_env_proxy_proto_msgTypes,
	}.Build()
	File_control_env_proxy_proto = out.File
	file_control_env_proxy_proto_rawDesc = nil
	file_control_env_proxy_proto_goTypes = nil
	file_control_env_proxy_proto_depIdxs = nil
}
