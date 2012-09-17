using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Reflection;
using System.Web.Script.Serialization;

namespace Berkelium.Managed {
    public class ExternalHostListener : IDisposable {
		public event ExternalHostHandler Unhandled;

        public readonly Window Window;
		public Dictionary<string, ExternalHostHandler> RegisteredHandlers;

        public ExternalHostListener (Window window) {
			RegisteredHandlers = new Dictionary<string, ExternalHostHandler>();
            Window = window;
            Window.ExternalHost += GlobalHandler;
        }

        public void Dispose () {
            RegisteredHandlers.Clear();
			Window.ExternalHost -= GlobalHandler;
        }

		protected void GlobalHandler(Window window, string msg, string origin, string target)
		{
			ExternalHostHandler handler;
            if (RegisteredHandlers.TryGetValue(msg, out handler)) {
				handler(window, msg, origin, target);
                return;
            }

            if (Unhandled != null)
				Unhandled(window, msg, origin, target);
        }

		public void Register(string msg, ExternalHostHandler handler)
		{
            RegisteredHandlers[msg] = handler;
        }

        public void Register (string msg, Action<string> handler) {
            Register(msg, (w, m, o, t) => handler(m));
        }

        public void Register (string msg, Action handler) {
            Register(msg, (w, m, o, t) => handler());
        }

        public void Unregister (string msg) {
            RegisteredHandlers.Remove(msg);
        }
    }
}
