// This is the main DLL file.

#include "stdafx.h"

#include "BerkeliumSharp.h"

using namespace System::Text;
using namespace System::Resources;
using namespace System::Reflection;

namespace Berkelium {
  namespace Managed {
    void BerkeliumSharp::Init (String ^ homeDirectory) {

        if (IsInitialized)
          return;

		const wchar_t* chars = (const wchar_t*)(Marshal::StringToHGlobalUni(homeDirectory)).ToPointer();

		// When in Debug mode, we disable the processus that makes a copy of
		// all the assemblies needed by the application. This should help with
		// our problem of not beeing able to attach the debugger to all the Berkelium code.

#if !DEBUG_BERKELIUM

        String ^ dllDirectory;

        if (homeDirectory != nullptr) {
          if (!Directory::Exists(homeDirectory))
            Directory::CreateDirectory(homeDirectory);

          if (!Directory::Exists(homeDirectory))
            throw gcnew ArgumentException(
              "The specified home directory was not found and could not be created."
            );

          dllDirectory = Path::Combine(homeDirectory, "NativeLibraries");
        } else {
          dllDirectory = Path::Combine(
            Environment::GetFolderPath(
              System::Environment::SpecialFolder::LocalApplicationData
            ), "BerkeliumSharp"
          );
        }

        if (!Directory::Exists(dllDirectory))
          Directory::CreateDirectory(dllDirectory);

		String ^ localesDirectory;
		localesDirectory = Path::Combine(dllDirectory, "locales");

        if (!Directory::Exists(localesDirectory))
          Directory::CreateDirectory(localesDirectory);

        Assembly ^ assembly = Assembly::GetExecutingAssembly();
        DateTime fileTime = File::GetLastWriteTimeUtc(assembly->Location);

        array<unsigned char> ^ buffer = gcnew array<unsigned char>(32768);
        for each (String ^ name in assembly->GetManifestResourceNames()) {
          bool shouldExtract = false;

          String ^ outputPath = Path::Combine(dllDirectory, name);

		  if(name == "en-US.dll")
			  outputPath = Path::Combine(localesDirectory, name);

          if (!File::Exists(outputPath))
            shouldExtract = true;
          else if (File::GetLastWriteTimeUtc(outputPath) < fileTime)
            shouldExtract = true;

          if (!shouldExtract)
            continue;

          {
            OutputDebugString(L"Extracting ");
            pin_ptr<const wchar_t> namePtr(PtrToStringChars(name));
            OutputDebugString(namePtr);
            OutputDebugString(L"... ");
          }

          Stream ^ inputStream = assembly->GetManifestResourceStream(name);
          try {
            Stream ^ outputStream = gcnew FileStream(
              outputPath, FileMode::Create, FileAccess::Write, FileShare::None
            );

            outputStream->SetLength(inputStream->Length);
            try {
              while (true) {
                  int readBytes = inputStream->Read(buffer, 0, buffer->Length);
                  if (readBytes <= 0)
                      break;
                  outputStream->Write(buffer, 0, readBytes);
              }
            } finally {
              outputStream->Close();
            }            
          } finally {
            inputStream->Close();
          }

          File::SetCreationTimeUtc(outputPath, fileTime);
          File::SetLastWriteTimeUtc(outputPath, fileTime);

          OutputDebugString(L"done.\r\n");
        }

        {
          pin_ptr<const wchar_t> dllDirPtr(PtrToStringChars(dllDirectory));
          SetDllDirectory(dllDirPtr);
          
          wchar_t pathBuf[4096];
          memset(pathBuf, 0, sizeof(wchar_t) * 4096);
          GetEnvironmentVariable(L"PATH", pathBuf, 4096);
          wcsncat_s(pathBuf, 4096, L";", 1);
          wcsncat_s(pathBuf, 4096, dllDirPtr, dllDirectory->Length);
          wcsncat_s(pathBuf, 4096, L";", 1);
          SetEnvironmentVariable(L"PATH", pathBuf);
        }
#else
		// If we're debugging Berkelium, we use the DLLs where they've been built
		// otherwise the debugger has a hard time attaching to them.
		SetDllDirectory(chars);
#endif

		FileString fileString = FileString::point_to(chars);

		try
		{
			::Berkelium::init(fileString);
		}
		catch(ExternalException ^ e)
		{
		}

		Marshal::FreeHGlobal(IntPtr((void*)chars));

        Wrapper = new ErrorDelegateWrapper();
        ::Berkelium::setErrorHandler(Wrapper);

        IsInitialized = true;
    }

    void GrowBufferForText (Decoder ^ decoder, const char * source, size_t length, wchar_t * &target, size_t &targetSize) {
      int count = decoder->GetCharCount((unsigned char *)source, length, true);

      if (targetSize < count) {
        if (target != 0)
          free(target);

        target = (wchar_t *)malloc(count * sizeof(wchar_t));
        targetSize = count;
      }
    }

    static void CopyToGlobal (array<unsigned char> ^ source, HGLOBAL & target) {
      if (source != nullptr) {
        pin_ptr<unsigned char> ptr = &source[0];
        target = Marshal::AllocHGlobal(source->Length).ToPointer();
        memcpy(target, ptr, source->Length);
      } else
        target = 0;
    }

    ProtocolHandler::ProtocolHandler (
      Managed::Context ^ context, 
      String ^ scheme
    )
      : Scheme(scheme)
      , Context(context)
    {
      Native = new NativeProtocolHandler(this);

      IntPtr schemePtr = Marshal::StringToHGlobalAnsi(scheme);

	  context->Native->registerProtocol((const char *)schemePtr.ToPointer(), scheme->Length, Native);

      Marshal::FreeHGlobal(schemePtr);
    }

    ProtocolHandler::~ProtocolHandler () {
      if (Native) {
        IntPtr schemePtr = Marshal::StringToHGlobalAnsi(Scheme);
        Context->Native->unregisterProtocol((const char *)schemePtr.ToPointer(), Scheme->Length);
        Marshal::FreeHGlobal(schemePtr);

        delete Native;
        Native = 0;
        
        Scheme = nullptr;
        Context = nullptr;
      }
    }

    bool NativeProtocolHandler::HandleRequest(const wchar_t * url, size_t urlLength, HGLOBAL &responseBody, HGLOBAL &responseHeaders) {
      return Owner->DoHandleRequest(url, urlLength, responseBody, responseHeaders);
    }

    bool ProtocolHandler::DoHandleRequest(const wchar_t * urlPtr, size_t urlLength, HGLOBAL &responseBody, HGLOBAL &responseHeaders) {
      String ^ url = gcnew String(urlPtr, 0, urlLength);
      array<unsigned char> ^ body = nullptr;
      array<String ^> ^ headers = nullptr;

      bool result = this->HandleRequest(
        url, body, headers
      );

      CopyToGlobal(body, responseBody);

      if (headers == nullptr)
          responseHeaders = 0;
      else {
        {
          int sz = 1;
          for (int i = 0; i < headers->Length; i++)
            sz += headers[i]->Length + 1;
          responseHeaders = Marshal::AllocHGlobal(sz).ToPointer();
          memset(responseHeaders, 0, sz);
        }

        {
          int pos = 0;
          unsigned char * ptr = (unsigned char *)(void *)responseHeaders;
          for (int i = 0; i < headers->Length; i++) {
            int len = headers[i]->Length;
            IntPtr headerPtr = Marshal::StringToHGlobalAnsi(headers[i]);
            memcpy(ptr + pos, headerPtr.ToPointer(), len);
            Marshal::FreeHGlobal(headerPtr);
            pos += len + 1;
          }
        }
      }

      return result;
    }

    Context ^ Context::GetContext(::Berkelium::Context * context, bool ownsHandle) {
      if (!Table)
        Table = new ContextTable();

      ContextTable::iterator iter = Table->find(context);

      if (iter != Table->end())
        return iter->second;

      Context ^ result = gcnew Context(context, ownsHandle);
      Table->operator [](context) = result;

      return result;
    }

    bool Context::ContextDestroyed (::Berkelium::Context * context) {
      ContextTable::iterator iter = Table->find(context);

      if (iter != Table->end()) {
        Table->erase(iter);
        return true;
      }

      return false;
    }

    void ErrorDelegateWrapper::onPureCall() {
      BerkeliumSharp::OnPureCall();
    }

    void ErrorDelegateWrapper::onInvalidParameter(const wchar_t *expression, const wchar_t *function, const wchar_t *file, unsigned int line, uintptr_t reserved) {
      BerkeliumSharp::OnInvalidParameter(
        gcnew String(expression),
        gcnew String(function),
        gcnew String(file),
        line
        );
    }

    void ErrorDelegateWrapper::onOutOfMemory() {
      BerkeliumSharp::OnOutOfMemory();
    }

    void ErrorDelegateWrapper::onAssertion(const char *assertMessage) {
      BerkeliumSharp::OnAssertion(
        gcnew String(assertMessage)
        );
    }

    Widget ^ WindowDelegateWrapper::GetWidget (::Berkelium::Widget * widget, bool ownsHandle) {
      TWidgetTable::iterator iter = WidgetTable.find(widget);

      if (iter != WidgetTable.end())
        return iter->second;

      return (WidgetTable[widget] = gcnew Widget(Owner, widget, ownsHandle));
    }

    bool WindowDelegateWrapper::WidgetDestroyed (::Berkelium::Widget * widget) {
      TWidgetTable::iterator iter = WidgetTable.find(widget);

      if (iter != WidgetTable.end()) {
        WidgetTable.erase(iter);
        return true;
      }

      return false;
    }

    void WindowDelegateWrapper::onCursorUpdated (::Berkelium::Window *win, const Cursor& newCursor) {
      Owner->OnCursorChanged(
        (IntPtr)newCursor.GetCursor()
      );
    }

    void WindowDelegateWrapper::onAddressBarChanged (::Berkelium::Window *win, ::Berkelium::URLString newURL) {
      Owner->OnAddressBarChanged(gcnew String(newURL.data(), 0, newURL.length()));
    }

    void WindowDelegateWrapper::onStartLoading (::Berkelium::Window *win, ::Berkelium::URLString newURL) {
      Owner->OnStartLoading(gcnew String(newURL.data(), 0, newURL.length()));
    }

    void WindowDelegateWrapper::onLoad (::Berkelium::Window *win) {
      Owner->OnLoad();
    }

    void WindowDelegateWrapper::onProvisionalLoadError(::Berkelium::Window *win, ::Berkelium::URLString url, int errorCode, bool isMainFrame) {
      Owner->OnProvisionalLoadError(
        gcnew String(url.data(), 0, url.length()), errorCode, isMainFrame
        );
    }

    void WindowDelegateWrapper::onCrashed (::Berkelium::Window *win) {
      Owner->OnCrashed();
    }

    void WindowDelegateWrapper::onUnresponsive (::Berkelium::Window *win) {
      Owner->OnUnresponsive();
    }

    void WindowDelegateWrapper::onResponsive (::Berkelium::Window *win) {
      Owner->OnResponsive();
    }

    void WindowDelegateWrapper::onExternalHost (::Berkelium::Window *win, ::Berkelium::WideString message, ::Berkelium::URLString origin, ::Berkelium::URLString target) {
	  Owner->OnExternalHost(
		gcnew String(message.data(), 0, message.length()),
		gcnew String(origin.data(), 0, origin.length()),
		gcnew String(target.data(), 0, target.length())
      );
    }

    void WindowDelegateWrapper::onCreatedWindow (::Berkelium::Window *win, ::Berkelium::Window *newWindow, ::Berkelium::Rect &initialRect) {
      Owner->OnCreatedWindow(
        gcnew Window(Owner->Context, newWindow, true),
        gcnew Rect(initialRect.left(), initialRect.top(), initialRect.width(), initialRect.height())
	  );
    }

    void WindowDelegateWrapper::onPaint (::Berkelium::Window *win, const unsigned char *sourceBuffer, const ::Berkelium::Rect &rect, int dx, int dy, const ::Berkelium::Rect &scrollRect) {

		Rect^ tempRect = gcnew Rect(0, 0, 0, 0);

		try
		{
			tempRect->Left = scrollRect.mLeft;
			tempRect->Top = scrollRect.mTop;
			tempRect->Width = scrollRect.mWidth;
			tempRect->Height = scrollRect.mHeight;
		}
		catch(Exception ^e)
		{

		}

		try
		{		
			Owner->OnPaint(
				IntPtr((void *)sourceBuffer),
				gcnew Rect(rect.left(), rect.top(), rect.width(), rect.height()),
				dx, dy,
				tempRect
			  );
		}
		catch(Exception ^e)
		{
		}
    }

    void WindowDelegateWrapper::onCrashedWorker(::Berkelium::Window *win) {
      Owner->OnCrashedWorker();
    }

    void WindowDelegateWrapper::onCrashedPlugin(::Berkelium::Window *win, ::Berkelium::WideString pluginName) {
      Owner->OnCrashedPlugin(gcnew String(pluginName.data(), 0, pluginName.length()));
    }

    void WindowDelegateWrapper::onConsoleMessage(::Berkelium::Window *win, ::Berkelium::WideString message, ::Berkelium::WideString sourceId, int line_no) {
      Owner->OnConsoleMessage(
		  gcnew String(sourceId.data(), 0, sourceId.length()),
		  gcnew String(message.data(), 0, message.length()),
        line_no
        );
    }

    void WindowDelegateWrapper::onScriptAlert(::Berkelium::Window *win, ::Berkelium::WideString message, ::Berkelium::WideString defaultValue, ::Berkelium::URLString url, int flags, bool &success, ::Berkelium::WideString &value) {
      String ^ valueStr = nullptr;
      Owner->OnScriptAlert(
		  gcnew String(message.data(), 0, message.length()),
		  gcnew String(defaultValue.data(), 0, defaultValue.length()),
		  gcnew String(url.data(), 0, url.length()),
        flags,
        success,
        valueStr
        );

      if (valueStr != nullptr) {
        pin_ptr<const wchar_t> valuePtr = PtrToStringChars(valueStr);
		value.point_to(valuePtr);
      }
    }

    void WindowDelegateWrapper::onNavigationRequested(::Berkelium::Window *win, URLString newUrl, URLString referrer, bool isNewWindow, bool &cancelDefaultAction) {
      Owner->OnNavigationRequested(
		  gcnew String(newUrl.data(), 0, newUrl.length()),
		  gcnew String(referrer.data(), 0, referrer.length()),
        isNewWindow, cancelDefaultAction
        );
    }

    void WindowDelegateWrapper::onWidgetCreated (::Berkelium::Window *win, ::Berkelium::Widget *newWidget, int zIndex) {
      if (newWidget->getId() == win->getId())
        return;

      Owner->OnWidgetCreated(
        GetWidget(newWidget, false), zIndex
      );
    }

    void WindowDelegateWrapper::onWidgetDestroyed (::Berkelium::Window *win, ::Berkelium::Widget *widget) {
      if (widget->getId() == win->getId())
        return;

      Owner->OnWidgetDestroyed(
        GetWidget(widget, false)
      );
      WidgetDestroyed(widget);
    }

    void WindowDelegateWrapper::onWidgetResize (::Berkelium::Window *win, ::Berkelium::Widget *widget, int newWidth, int newHeight) {
      if (widget->getId() == win->getId())
        return;

      Owner->OnWidgetResized(
        GetWidget(widget, false),
        newWidth, newHeight
      );
    }

    void WindowDelegateWrapper::onWidgetMove (::Berkelium::Window *win, ::Berkelium::Widget *widget, int newX, int newY) {
      if (widget->getId() == win->getId())
        return;

      Owner->OnWidgetMoved(
        GetWidget(widget, false),
        newX, newY
      );
    }

    void WindowDelegateWrapper::onWidgetPaint (::Berkelium::Window *win, ::Berkelium::Widget *widget, const unsigned char *sourceBuffer, const ::Berkelium::Rect &rect, int dx, int dy, const ::Berkelium::Rect &scrollRect) {
      if (widget->getId() == win->getId())
        return;

      Owner->OnWidgetPaint(
        GetWidget(widget, false),
        IntPtr((void *)sourceBuffer),
        gcnew Rect(rect.left(), rect.top(), rect.width(), rect.height()),
        dx, dy,
        gcnew Rect(scrollRect.left(), scrollRect.top(), scrollRect.width(), scrollRect.height())
      );
    }

    void WindowDelegateWrapper::onLoadingStateChanged(::Berkelium::Window *win, bool isLoading) {
      Owner->OnLoadingStateChanged(
        isLoading
      );
    }

    void WindowDelegateWrapper::onTitleChanged(::Berkelium::Window *win, ::Berkelium::WideString title) {
      Owner->OnTitleChanged(
		  gcnew String(title.data(), 0, title.length())
      );
    }

    void WindowDelegateWrapper::onTooltipChanged(::Berkelium::Window *win, ::Berkelium::WideString tooltip) {
      Owner->OnTooltipChanged(
		  gcnew String(tooltip.data(), 0, tooltip.length())
      );
    }

    void WindowDelegateWrapper::onShowContextMenu(::Berkelium::Window *win, const ::Berkelium::ContextMenuEventArgs& cargs) {
      ContextMenuEventArgs ^ args = gcnew ContextMenuEventArgs();

	  const char* jojo = cargs.linkUrl.data();

      args->MediaType = (MediaType)cargs.mediaType;
      args->MouseX = cargs.mouseX;
      args->MouseY = cargs.mouseY;
	  args->LinkUrl = gcnew String(jojo);
	  args->SrcUrl = gcnew String(cargs.srcUrl.data(), 0, cargs.srcUrl.length());
	  args->PageUrl = gcnew String(cargs.pageUrl.data(), 0, cargs.pageUrl.length());
	  args->FrameUrl = gcnew String(cargs.frameUrl.data(), 0, cargs.frameUrl.length());
	  args->SelectedText = gcnew String(cargs.selectedText.data(), 0, cargs.selectedText.length());
      args->IsEditable = cargs.isEditable;
      args->EditFlags = (EditFlags)cargs.editFlags;

      Owner->OnShowContextMenu(
        args
      );
    }

	void WindowDelegateWrapper::onJavascriptCallback(::Berkelium::Window *win, void* replyMsg, ::Berkelium::URLString origin, ::Berkelium::WideString funcName, Script::Variant *args, size_t numArgs) {
		Owner->OnJavascriptCallback(
			replyMsg,
			gcnew String(origin.data(), 0, origin.length()),
			gcnew String(funcName.data(), 0, funcName.length()),
			args,
			numArgs);
	}

	void WindowDelegateWrapper::onRunFileChooser(::Berkelium::Window *win, int mode, ::Berkelium::WideString title, ::Berkelium::FileString defaultFile) {
		Owner->OnRunFileChooser(
			mode,
			gcnew String(title.data(), 0, title.length()),
			gcnew String(defaultFile.data(), 0, defaultFile.length()));
	}

		// ScriptVariant:
		ScriptVariant::ScriptVariant(Berkelium::Script::Variant variant)
		{
			Native = new Berkelium::Script::Variant(variant);
		}

		ScriptVariant::ScriptVariant() {
			Native = new Berkelium::Script::Variant();
		}

		ScriptVariant::ScriptVariant(System::String ^ str)
		{
			const wchar_t* strChars = (const wchar_t*)((Marshal::StringToHGlobalUni(str)).ToPointer());
			WideString strUrlString = WideString::point_to(strChars);

			Native = new Berkelium::Script::Variant(strUrlString);

			Marshal::FreeHGlobal(IntPtr((void*)strChars));
		}

		ScriptVariant::ScriptVariant(System::Double dblval) {
			Native = new Berkelium::Script::Variant(dblval);
		}

		ScriptVariant::ScriptVariant(System::Int32 intval) {
			double value = intval;
			Native = new Berkelium::Script::Variant(value);
		}

		ScriptVariant::ScriptVariant(System::Boolean boolval) {
			Native = new Berkelium::Script::Variant(boolval);
		}

		Boolean ScriptVariant::ToBoolean()
		{
			return Native->toBoolean();
		}

		Int32 ScriptVariant::ToInteger()
		{
			return Native->toInteger();
		}

		Double ScriptVariant::ToDouble()
		{
			return Native->toDouble();
		}

		String^ ScriptVariant::ToStringValue()
		{
			const Berkelium::WideString wideStringValue = Native->toString();
			String ^returnValue = gcnew String(wideStringValue.mData, 0, wideStringValue.mLength);
			return returnValue;
		}

		String^ ScriptVariant::ToFunctionName()
		{
			String ^returnValue = gcnew String(Native->toFunctionName().mData);
			return returnValue;
		}
  }
}