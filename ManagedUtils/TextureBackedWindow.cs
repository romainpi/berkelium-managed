using System;
using System.Collections.Generic;
using System.Linq;
using Microsoft.Xna.Framework.Graphics;
using System.Web.Script.Serialization;
using System.Text.RegularExpressions;
using System.Threading;
using Point = Microsoft.Xna.Framework.Point;
using Rectangle = Microsoft.Xna.Framework.Rectangle;

namespace Berkelium.Managed
{
	public class TextureBackedWindow : Window
	{
		private static readonly Regex ArgumentRegex = new Regex(@"\$(?'id'[0-9]*)", RegexOptions.Compiled);

		private readonly Queue<Texture2D> _deadTextures;

		public object Lock = null;
		public readonly GraphicsDevice Device;
		public Texture2D Texture;
		public JavaScriptSerializer Serializer;

		new public ExternalHostListener ExternalHost;

		private readonly Dictionary<Widget, Texture2D> _widgetTextures;

		public TextureBackedWindow(Context context, GraphicsDevice device)
			: base(context)
		{

			Device = device;
			Transparent = true;
			_deadTextures = new Queue<Texture2D>();
			_widgetTextures = new Dictionary<Widget, Texture2D>();
			ExternalHost = new ExternalHostListener(this);
			Serializer = new JavaScriptSerializer();
		}

		public override void Resize(int width, int height)
		{
			if ((width == Width) && (height == Height))
				return;

			if (Lock != null)
				Monitor.Enter(Lock);

			var oldTexture = Texture;
			Texture = null;
			var newTexture = new Texture2D(
				Device, width, height, 1,
				TextureUsage.Linear, SurfaceFormat.Color
			);

			if (oldTexture != null)
			{
				int w = Math.Min(oldTexture.Width, newTexture.Width);
				int h = Math.Min(oldTexture.Height, newTexture.Height);
				int sz = w * h;

				var temporaryBuffer = new int[sz];

				oldTexture.GetData(0, new Rectangle(0, 0, w, h), temporaryBuffer, 0, sz);
				newTexture.SetData(0, new Rectangle(0, 0, w, h), temporaryBuffer, 0, sz, SetDataOptions.Discard);
				oldTexture.Dispose();
			}

			Texture = newTexture;

			if (Lock != null)
				Monitor.Exit(Lock);

			BerkeliumManaged.Update();

			base.Resize(width, height);

			BerkeliumManaged.Update();
		}

		public void Cleanup()
		{
			while (_deadTextures.Count > 0)
				lock (Lock)
					_deadTextures.Dequeue().Dispose();
		}

		public void ExecuteJavascript(string javascript, params object[] variables)
		{
			var serializedVariables = (from v in variables select Serializer.Serialize(v)).ToArray();

			javascript = ArgumentRegex.Replace(
				javascript, m => serializedVariables[int.Parse(m.Groups["id"].Value)]
			);

			base.ExecuteJavascript(javascript);
		}

		protected override void OnWidgetCreated(Widget widget, int zIndex)
		{
			OnWidgetResized(widget, widget.Rect.Width, widget.Rect.Height);

			base.OnWidgetCreated(widget, zIndex);
		}

		protected override void OnWidgetResized(Widget widget, int newWidth, int newHeight)
		{
			Texture2D texture;
			if (_widgetTextures.TryGetValue(widget, out texture))
				texture.Dispose();

			texture = new Texture2D(
				Device, newWidth, newHeight, 1,
				TextureUsage.Linear, SurfaceFormat.Color
			);
			_widgetTextures[widget] = texture;

			base.OnWidgetResized(widget, newWidth, newHeight);
		}

		protected override void OnWidgetDestroyed(Widget widget)
		{
			if (Lock != null)
				Monitor.Enter(Lock);

			Texture2D texture;

			if (_widgetTextures.TryGetValue(widget, out texture))
			{
				_deadTextures.Enqueue(texture);
				_widgetTextures.Remove(widget);
			}

			if (Lock != null)
				Monitor.Exit(Lock);

			base.OnWidgetDestroyed(widget);
		}

		protected override void OnPaint(IntPtr sourceBuffer, Rect rect, int dx, int dy, Rect scrollRect)
		{
			HandlePaintEvent(Texture, sourceBuffer, rect, dx, dy, scrollRect);

			base.OnPaint(sourceBuffer, rect, dx, dy, scrollRect);
		}

		protected override void OnWidgetPaint(Widget widget, IntPtr sourceBuffer, Rect rect, int dx, int dy, Rect scrollRect)
		{
			Texture2D texture;
			if (_widgetTextures.TryGetValue(widget, out texture))
				HandlePaintEvent(texture, sourceBuffer, rect, dx, dy, scrollRect);

			base.OnWidgetPaint(widget, sourceBuffer, rect, dx, dy, scrollRect);
		}

		protected void HandlePaintEvent(Texture2D texture, IntPtr sourceBuffer, Rect rect, int dx, int dy, Rect scrollRect)
		{
			if (Lock != null)
				Monitor.Enter(Lock);

			var clientRect = new Rectangle(rect.Left, rect.Top, rect.Width, rect.Height);

			Device.Textures[0] = null;

			if ((clientRect.Right <= texture.Width) && (clientRect.Bottom <= texture.Height))
			{
				unsafe
				{
					texture.SetData(0, sourceBuffer.ToPointer(), texture.Width, texture.Height, (uint)(texture.Width * 4), D3DFORMAT.A8R8G8B8);
				}
			}

			Device.Textures[0] = texture;

			if (Lock != null)
				Monitor.Exit(Lock);
		}

		public IEnumerable<KeyValuePair<Texture2D, Point>> RenderList
		{
			get
			{
				yield return new KeyValuePair<Texture2D, Point>(
					Texture, new Point(0, 0)
				);

				foreach (var kvp in _widgetTextures)
				{
					var rect = kvp.Key.Rect;

					yield return new KeyValuePair<Texture2D, Point>(
						kvp.Value, new Point(rect.Left, rect.Top)
					);
				}
			}
		}

		protected override void Dispose(bool __p1)
		{
			if (Lock != null)
				Monitor.Enter(Lock);

			if (Texture != null)
			{
				Texture.Dispose();
				Texture = null;
			}

			if (Lock != null)
				Monitor.Exit(Lock);

			base.Dispose(__p1);
		}
	}
}
