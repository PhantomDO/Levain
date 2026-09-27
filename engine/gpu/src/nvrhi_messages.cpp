#include "nvrhi_messages.hpp"

#include "levain/core/assert.hpp"
#include "levain/core/log.hpp"

namespace levain::gpu
{

namespace
{

core::LogLevel toLogLevel(nvrhi::MessageSeverity severity)
{
    switch (severity)
    {
    case nvrhi::MessageSeverity::Info:
        return core::LogLevel::Info;
    case nvrhi::MessageSeverity::Warning:
        return core::LogLevel::Warning;
    case nvrhi::MessageSeverity::Error:
        return core::LogLevel::Error;
    case nvrhi::MessageSeverity::Fatal:
        return core::LogLevel::Critical;
    }
    return core::LogLevel::Critical;
}

class NvrhiMessages final : public nvrhi::IMessageCallback
{
public:
    void message(nvrhi::MessageSeverity severity, const char* messageText) override
    {
        core::log("nvrhi", toLogLevel(severity), "{}", messageText);
        LEVAIN_ASSERT(severity < nvrhi::MessageSeverity::Error, "erreur NVRHI, voir ci-dessus");
    }
};

} // namespace

nvrhi::IMessageCallback& nvrhiMessages()
{
    static NvrhiMessages messages;
    return messages;
}

} // namespace levain::gpu
