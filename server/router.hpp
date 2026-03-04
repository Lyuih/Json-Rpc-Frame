#pragma once
/**
 * 实现了Json-RPC服务端的服务管理与路由机制
 */

#include "../common/net.hpp"
#include "../common/message.hpp"

namespace Lyuih
{
    namespace server
    {
        // 定义Josn-RPC参数和返回值类型
        enum class VType
        {
            BOOL = 0,
            INTEGRAL,
            NUMERIC,
            STRING,
            ARRAY,
            OBJECT
        };
        // 描述一个RPC服务，包含方法名、参数格式、返回类型和业务逻辑回调
        class ServerDescribe
        {
        public:
            using ptr = std::shared_ptr<ServerDescribe>;
            using ParamsDescribe = std::pair<std::string, VType>;
            using ServiceCallback = std::function<void(const Json::Value &, Json::Value &)>;
            ServerDescribe(std::string &&method_name, std::vector<ParamsDescribe> &&param_desc, const VType return_type, ServiceCallback &&service_cb)
                : method_name_(std::move(method_name)), param_desc_(std::move(param_desc)), return_type_(return_type), service_cb_(std::move(service_cb))
            {
            }
            std::string method()
            {
                return method_name_;
            }
            // 校验请求参数的完整性和类型。
            bool paramCheck(const Json::Value &param)
            {
                for (auto &desc : param_desc_)
                {
                    // 1.检查是否存在参数
                    if (param.isMember(desc.first) == false)
                    {
                        LOG_ERROR("{}字段缺失,参数完整性校验失败", desc.first);
                        return false;
                    }
                    // 2.检查参数类型
                    if (check(desc.second, param[desc.first]) == false)
                    {
                        LOG_ERROR("{}字段类型校验失败", desc.first);
                        return false;
                    }
                }
                return true;
            }
            bool call(const Json::Value &param, Json::Value &ret)
            {
                service_cb_(param, ret);
                if (rTypeCheck(ret) == false)
                {
                    LOG_ERROR("回调函数中响应消息检验失败");
                    return false;
                }
                return true;
            }

        private:
            bool rTypeCheck(const Json::Value &val)
            {
                return check(return_type_, val);
            }
            // 类型检查
            bool check(VType vtype, const Json::Value &val)
            {
                switch (vtype)
                {
                case VType::BOOL:
                    return val.isBool();
                case VType::INTEGRAL:
                    return val.isIntegral();
                case VType::NUMERIC:
                    return val.isNumeric();
                case VType::STRING:
                    return val.isString();
                case VType::ARRAY:
                    return val.isArray();
                case VType::OBJECT:
                    return val.isObject();
                }
                return false;
            }

        private:
            std::string method_name_;                // 方法名
            std::vector<ParamsDescribe> param_desc_; // 参数格式
            VType return_type_;                      // 返回类型
            ServiceCallback service_cb_;             // 业务逻辑回调
        };

        class SDescribeFactory
        {
        public:
            using ptr = std::shared_ptr<SDescribeFactory>;
            void setMethodName(const std::string &name)
            {
                method_name_ = name;
            }
            void PushParam(const std::string &pname, VType vtype)
            {
                param_desc_.emplace_back(pname, vtype);
            }
            void setCallback(const ServerDescribe::ServiceCallback &service_cb)
            {
                service_cb_ = service_cb;
            }
            void setRetType(VType ret_type)
            {
                return_type_ = ret_type;
            }
            ServerDescribe::ptr build()
            {
                return std::make_shared<ServerDescribe>(std::move(method_name_), std::move(param_desc_), return_type_, std::move(service_cb_));
            }

        private:
            std::string method_name_;                                // 方法名
            std::vector<ServerDescribe::ParamsDescribe> param_desc_; // 参数格式
            VType return_type_;                                      // 返回类型
            ServerDescribe::ServiceCallback service_cb_;             // 业务逻辑回调
        };
        // 管理所有注册的RPC服务，基于方法名进行插入、查找和删除
        class ServiceManager
        {
        public:
            using ptr = std::shared_ptr<ServiceManager>;
            void insert(const ServerDescribe::ptr &service)
            {
                std::unique_lock<std::mutex> lock(mutex_);
                services_.insert_or_assign(service->method(), service);
            }
            ServerDescribe::ptr select(const std::string &name)
            {
                std::unique_lock<std::mutex> lock(mutex_);
                auto it = services_.find(name);
                if (it == services_.end())
                {
                    LOG_ERROR("{}服务不存在", name);
                    return ServerDescribe::ptr();
                }
                return it->second;
            }
            void remove(const std::string &name)
            {
                std::unique_lock<std::mutex> lock(mutex_);
                auto it = services_.find(name);
                if (it == services_.end())
                {
                    LOG_ERROR("{}服务不存在", name);
                    return;
                }
                services_.erase(name);
            }

        private:
            std::mutex mutex_;
            std::unordered_map<std::string, ServerDescribe::ptr> services_;
        };

        // 处理RpcRequest消息，路由到对应服务，执行校验和业务逻辑，生成响应。
        class RpcRouter
        {
        public:
            using ptr = std::shared_ptr<RpcRouter>;
            RpcRouter()
                : service_manager_(std::make_shared<ServiceManager>())
            {
            }
            void onRpcRequest(const BaseConnection::ptr &conn, const RpcRequest::ptr &request)
            {
                // 1.查询客户请求的方法描述,判断当前服务端是否能提供对应服务
                auto method = request->method();
                auto service = service_manager_->select(method);
                if (service == ServerDescribe::ptr())
                {
                    LOG_ERROR("{}服务未找到", method);
                    responsee(conn, request, Json::Value(), RCode::RCODE_NOT_FOUND_SERVICE);
                    return;
                }
                // 2.参数校验,确定是否能提供服务
                if (service->paramCheck(request->param()) == false)
                {
                    LOG_ERROR("{}服务参数校验失败", method);
                    responsee(conn, request, Json::Value(), RCode::RCODE_INVALID_PARAMS);
                    return;
                }
                // 3.调用业务回调接口进行业务处理
                Json::Value result;
                bool ret = service->call(request->param(), result);
                if (ret == false)
                {
                    LOG_ERROR("{}回调函数中响应消息检验失败", method);
                }
                // 4.处理完毕得到结果,组织响应,向客户端发送
                responsee(conn, request, result, RCode::RCODE_OK);
            }
            void resisterMethod(const ServerDescribe::ptr &service)
            {
                service_manager_->insert(service);
            }

        private:
            void responsee(const BaseConnection::ptr &conn, const RpcRequest::ptr &request, const Json::Value &res, RCode rcode)
            {
                auto msg = MessageFactory::create<RpcResponse>();
                msg->setId(request->Id());
                msg->setMType(MType::RSP_RPC);
                msg->setRcode(rcode);
                msg->setResult(res);
                conn->send(msg);
            }

        private:
            ServiceManager::ptr service_manager_;
        };
    };
};