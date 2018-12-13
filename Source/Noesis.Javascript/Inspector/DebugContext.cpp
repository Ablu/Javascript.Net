#include "DebugContext.h"

#include "v8.h"
#include "libplatform\libplatform.h"
#include "..\JavascriptContext.h"
#include "..\JavascriptInterop.h"
#include "..\JavascriptException.h"
#include "InspectorClient.h"
#include "BackChannelDelegate.h"

namespace Noesis
{
    namespace Javascript
    {
        namespace Debugging
        {
            const int MESSAGE_ID_START_COUNTER = 1;

            DebugContext::DebugContext(JavascriptContext^ javascriptContext)
            {
                this->javascriptContext = javascriptContext;
                
                this->debuggerStartSymbol = System::String::Format("DebuggerStart:{0}", System::Guid::NewGuid().ToString("B"));
                this->messageIdCounter = MESSAGE_ID_START_COUNTER;
                this->debuggerState = DebuggerState::Stopped;

                JavascriptScope javascriptScope(this->javascriptContext);

                // get context
                v8::Isolate *isolate = this->javascriptContext->GetCurrentIsolate();
                HandleScope scope(isolate);
                v8::Local<v8::Context> localContext = this->javascriptContext->GetV8Context()->Get(isolate);

                // notification handler
                BackChannelDelegate::MessageDelegate^ notifyCallback = gcnew BackChannelDelegate::MessageDelegate(this, &DebugContext::OnNotificationHandler);

                // Create inspector and connect it
                this->inspectorClient = new InspectorClient(*isolate, *this->javascriptContext->GetCurentPlatform());
                this->inspectorClient->ContextCreated(localContext, this->DEBUGGER_CONTEXT_NAME);
                BackChannelDelegate^ backChannelDelegate = gcnew BackChannelDelegate(notifyCallback);
                this->inspectorClient->ConnectFrontend(backChannelDelegate);
            }

            DebugContext::~DebugContext()
            {
                // try release debugger state
                if (this->debuggerState != DebuggerState::Stopped)
                    this->debuggerState = DebuggerState::Stopped;

                // disconnect (destroy context?) (implicite disable debugger)
                this->inspectorClient->DisconnectFrontend();

                // release debugger state
                this->debuggerState = DebuggerState::Stopped;
            }

            /*
            * Starts a new debugging session
            */
            System::Object^ DebugContext::Debug(System::String^ script, System::Action<System::String^>^ OnNotificationHandler)
            {
                return this->Debug(script, nullptr, OnNotificationHandler);
            }

            System::Object^ DebugContext::Debug(System::String^ script, System::String^ scriptResourceName, System::Action<System::String^>^ OnNotificationHandler)
            {
                if (script == nullptr)
                {
                    throw gcnew System::ArgumentNullException("script");
                }
                if (OnNotificationHandler == nullptr)
                {
                    throw gcnew System::ArgumentNullException("OnNotificationHandler");
                }
                if (this->debuggerState != DebuggerState::Stopped)
                {
                    throw gcnew System::InvalidOperationException("WrongDebuggerState");
                }

                this->debuggerState = DebuggerState::Initializing;
                this->ExternalOnNotificationHandler = OnNotificationHandler;
                try
                {
                    JavascriptScope javascriptScope(this->javascriptContext);
                    v8::Isolate *isolate = this->javascriptContext->GetCurrentIsolate();
                    HandleScope scope(isolate);

                    // sends event "Debugger.paused" on first statement
                    this->inspectorClient->SchedulePauseOnNextStatement(debuggerStartSymbol);

                    // run script
                    System::Object^ result = scriptResourceName == nullptr
                        ? this->javascriptContext->Run(script)
                        : this->javascriptContext->Run(script, scriptResourceName);

                    // release debugger state
                    this->debuggerState = DebuggerState::Stopped;

                    return result;
                }
                finally
                {
                    // ignore: must be disposed after usage
                }
            }

            void DebugContext::TerminateExecution()
            {
                if (this->debuggerState != DebuggerState::Started)
                {
                    throw gcnew System::InvalidOperationException("WrongDebuggerState");
                }
                this->inspectorClient->TerminateExecution();
            }

            System::String^ DebugContext::SendProtocolMessage(System::String^ message)
            {
                if (message == nullptr)
                {
                    throw gcnew System::ArgumentNullException("message");
                }
                if (this->debuggerState == DebuggerState::Initializing)
                {
                    throw gcnew System::InvalidOperationException("WrongDebuggerState");
                }
                
                if (this->debuggerState == DebuggerState::Stopped)
                {
                    JavascriptScope javascriptScope(this->javascriptContext);
                    v8::Isolate *isolate = this->javascriptContext->GetCurrentIsolate();
                    HandleScope scope(isolate);
                    this->inspectorClient->DispatchMessage(message);
                }
                else
                {
                    this->debuggerState = DebuggerState::Started;
                    this->inspectorClient->DispatchMessageFromFrontend(message);
                }

                // wait for response
                System::String^ response = this->inspectorClient->GetChannel().GetBackChannelDelegate()->WaitForResponse();

                return response;
            }

            unsigned int DebugContext::GetNextMessageId()
            {
                return this->messageIdCounter++;
            }
            
            void DebugContext::OnNotificationHandler(System::String^ message)
            {
                try
                {
                    if (this->debuggerState == DebuggerState::Initializing && message->Contains(this->debuggerStartSymbol))
                    {
                        this->debuggerState = DebuggerState::Started;
                    }
                    if (this->ExternalOnNotificationHandler != nullptr)
                    {
                        this->ExternalOnNotificationHandler(message);
                    }
                }
                catch (System::Exception^)
                {
                    // ignore to prevent unknown state
                }
            }
        }
    }
}