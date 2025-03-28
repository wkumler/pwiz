/*
 * Original author: Brendan MacLean <brendanx .at. u.washington.edu>,
 *                  MacCoss Lab, Department of Genome Sciences, UW
 *
 * Copyright 2012 University of Washington - Seattle, WA
 * 
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

using System;
using System.Collections.Generic;
using System.Drawing;
using System.Linq;
using System.Windows.Forms;
using pwiz.Skyline.Controls.SeqNode;
using pwiz.Skyline.Model;
using pwiz.Skyline.Properties;
using pwiz.Skyline.Util;
using pwiz.Skyline.Util.Extensions;
using ZedGraph;

namespace pwiz.Skyline.Controls.Graphs
{
    public abstract class SummaryBarGraphPaneBase : SummaryGraphPane
    {
        public class ToolTipImplementation : ITipProvider
        {
            public class TargetCurveList : List<CurveItem>
            {
                private SummaryBarGraphPaneBase _parent;

                public TargetCurveList(SummaryBarGraphPaneBase parent)
                {
                    _parent = parent;
                }

                public CurveItem ClearAndAdd(CurveItem curve)
                {
                    Clear();
                    Add(curve);
                    return curve;
                }

                public Axis GetYAxis()
                {
                    if (Count == 0)
                        return null;
                    return base[0].GetYAxis(_parent);
                }

                public bool IsTarget(CurveItem curve)
                {
                    return this.Any(c => ReferenceEquals(c, curve));
                }
            }

            private SummaryBarGraphPaneBase _parent;
            private bool _isVisible;
            private NodeTip _tip;
            private TableDesc _table;
            internal RenderTools RenderTools = new RenderTools();

            public double? YPosition { get; set; } //vertical coordinate of the tooltip
            public int ReplicateIndex { get; private set; }
            public TargetCurveList TargetCurves {  get; private set; }

            public ToolTipImplementation(SummaryBarGraphPaneBase parent)
            {
                _parent = parent;
                TargetCurves = new TargetCurveList(parent);
            }

            public ITipProvider TipProvider { get { return this; } }

            bool ITipProvider.HasTip => true;

            Size ITipProvider.RenderTip(Graphics g, Size sizeMax, bool draw)
            {
                var size = _table.CalcDimensions(g);
                if (draw)
                    _table.Draw(g);
                return new Size((int)size.Width + 2, (int)size.Height + 2);
            }

            public void AddLine(string description, string data)
            {
                if (_table == null)
                    _table = new TableDesc();
                _table.AddDetailRow(description, data, RenderTools, StringAlignment.Far);
            }

            public void ClearData()
            {
                _table?.Clear();
            }

            public void Draw(int dataIndex, Point cursorPos, CurveItem curve)
            {
                if (_isVisible)
                {
                    if (ReplicateIndex == dataIndex)
                        return;
                    Hide();
                }
                if (_table == null || _table.Count == 0 || !TargetCurves.Any()) return;

                ReplicateIndex = dataIndex;
                var basePoint = new UserPoint(dataIndex + 1,
                    (float)(YPosition ?? (curve.Points[ReplicateIndex].Y)) / _parent.YScale, _parent, curve.GetYAxis(_parent) ?? _parent.YAxis);

                using (var g = _parent.GraphSummary.GraphControl.CreateGraphics())
                {
                    var size = _table.CalcDimensions(g);
                    var offset = new Size(0, -(int)(size.Height + size.Height / _table.Count));
                    if (_tip == null)
                        _tip = new NodeTip(_parent);
                    _tip.SetTipProvider(TipProvider, new Rectangle(basePoint.Screen(offset), new Size()), cursorPos);
                }
                _isVisible = true;
            }

            public void Hide()
            {
                if (_isVisible)
                {
                    _tip?.HideTip();
                    _isVisible = false;
                }
            }

            #region Test Methods
            public List<string> TipLines
            {
                get
                {
                    return _table.Select((rowDesc) =>
                        string.Join(TextUtil.SEPARATOR_TSV_STR, rowDesc.Select(cell => cell.Text))
                    ).ToList();
                }
            }

            #endregion
            private class UserPoint
            {
                private GraphPane _graph;
                private Axis _yAxis;
                public int X { get; private set; }
                public float Y { get; private set; }

                public UserPoint(int x, float y, GraphPane graph)
                {
                    X = x;
                    Y = y;
                    _graph = graph;
                    _yAxis = graph.YAxis;
                }
                public UserPoint(int x, float y, GraphPane graph, Axis yAxis) : this(x, y, graph)
                {
                    if(yAxis is Y2Axis)
                        _yAxis = yAxis;
                }

                public PointF User()
                {
                    return new PointF(X, Y);
                }

                public Point Screen()
                {
                    return new Point(
                        (int)_graph.XAxis.Scale.Transform(X),
                        (int)_yAxis.Scale.Transform(Y));
                }
                public Point Screen(Size OffsetScreen)
                {
                    return new Point(
                        (int)(_graph.XAxis.Scale.Transform(X) + OffsetScreen.Width),
                        (int)(_yAxis.Scale.Transform(Y) + OffsetScreen.Height));
                }

                public PointF PF()
                {
                    return new PointF(
                        _graph.XAxis.Scale.Transform(X) / _graph.Rect.Width,
                        _yAxis.Scale.Transform(Y) / _graph.Rect.Height);
                }
                public PointD PF(SizeF OffsetPF)
                {
                    return new PointD(
                        _graph.XAxis.Scale.Transform(X) / _graph.Rect.Width + OffsetPF.Width,
                        _yAxis.Scale.Transform(Y) / _graph.Rect.Height + OffsetPF.Height);
                }
            }
        }

        public virtual void PopulateTooltip(int index, CurveItem targetCurve) {}

        /// <summary>
        /// Additional scaling factor for tooltip's vertical position.
        /// </summary>
        public virtual float YScale
        {
            get { return 1.0f; }
        }
        /// <summary>
        /// Create a new tooltip instance in the child class constructor if you
        /// want to show thw tooltips.
        /// </summary>
        public ToolTipImplementation ToolTip { get; protected set; }

        protected static bool ShowSelection
        {
            get
            {
                return Settings.Default.ShowReplicateSelection;
            }
        }

        protected static IList<Color> COLORS_TRANSITION {get { return GraphChromatogram.COLORS_LIBRARY; }}
        protected static IList<Color> COLORS_GROUPS {get { return GraphChromatogram.COLORS_GROUPS; }}

        protected static int GetColorIndex(PeptideDocNode peptideDocNode, TransitionGroupDocNode nodeGroup, int countLabelTypes)
        {
            return GraphChromatogram.GetColorIndex(peptideDocNode, nodeGroup, countLabelTypes);
        }

        protected SummaryBarGraphPaneBase(GraphSummary graphSummary)
            : base(graphSummary)
        {
            _axisLabelScaler = new AxisLabelScaler(this);
        }

        public void Clear()
        {
            CurveList.Clear();
            GraphObjList.Clear();
            ToolTip?.Hide();
        }

        protected virtual int FirstDataIndex { get { return 0; } }

        protected abstract int SelectedIndex { get; }

        protected abstract IdentityPath GetIdentityPath(CurveItem curveItem, int barIndex);

        public override bool HandleKeyDownEvent(object sender, KeyEventArgs keyEventArgs)
        {
            switch (keyEventArgs.KeyCode)
            {
                case Keys.Left:
                case Keys.Up:
                    ChangeSelection(SelectedIndex - 1, null);
                    return true;
                case Keys.Right:
                case Keys.Down:
                    ChangeSelection(SelectedIndex + 1, null);
                    return true;
            }
            return false;
        }

        protected abstract void ChangeSelection(int selectedIndex, IdentityPath identityPath);

        public override bool HandleMouseMoveEvent(ZedGraphControl sender, MouseEventArgs mouseEventArgs)
        {
            if (mouseEventArgs.Button != MouseButtons.None)
                return base.HandleMouseMoveEvent(sender, mouseEventArgs);

            CurveItem nearestCurve;
            int iNearest;
            if (!FindNearestPoint(new PointF(mouseEventArgs.X, mouseEventArgs.Y), out nearestCurve, out iNearest))
            {
                ToolTip?.Hide();
                var axis = GetNearestXAxis(sender, mouseEventArgs);
                if (axis != null)
                {
                    GraphSummary.Cursor = Cursors.Hand;
                    return true;
                }
                return false;
            }

            if (ToolTip != null && ToolTip.TargetCurves.IsTarget(nearestCurve))
            {
                PopulateTooltip(iNearest, nearestCurve);
                ToolTip.Draw(iNearest, mouseEventArgs.Location, nearestCurve);
                sender.Cursor = Cursors.Hand;
                return true;
            }
            else
                ToolTip?.Hide();

            IdentityPath identityPath = GetIdentityPath(nearestCurve, iNearest);
            if (identityPath == null)
            {
                return false;
            }
            GraphSummary.Cursor = Cursors.Hand;
            return true;
        }

        public override void HandleMouseOutEvent(object sender, EventArgs e)
        {
            ToolTip?.Hide();
        }

        public XAxis GetNearestXAxis(ZedGraphControl sender, MouseEventArgs mouseEventArgs)
        {
            using (Graphics g = sender.CreateGraphics())
            {
                object nearestObject;
                if (FindNearestObject(new PointF(mouseEventArgs.X, mouseEventArgs.Y), g, out nearestObject, out _))
                {
                    var axis = nearestObject as XAxis;
                    if (axis != null)
                        return axis;
                }
            }

            return null;
        }

        public override bool HandleMouseDownEvent(ZedGraphControl sender, MouseEventArgs mouseEventArgs)
        {
            CurveItem nearestCurve;
            int iNearest;
            var axis = GetNearestXAxis(sender, mouseEventArgs);
            if (axis != null)
            {
                iNearest = (int)axis.Scale.ReverseTransform(mouseEventArgs.X - axis.MajorTic.Size);
                if (iNearest < 0)
                    return false;
                ChangeSelection(iNearest, GraphSummary.StateProvider.SelectedPath);
                return true;
            }
            if (!FindNearestPoint(new PointF(mouseEventArgs.X, mouseEventArgs.Y), out nearestCurve, out iNearest))
            {
                return false;
            }
            IdentityPath identityPath = GetIdentityPath(nearestCurve, iNearest);
            if (identityPath == null)
            {
                return false;
            }

            ChangeSelection(iNearest, identityPath);
            return true;
        }

        public override void OnClose(EventArgs e)
        {
            if (ToolTip != null)
            {
                ToolTip.Hide();
                ToolTip.RenderTools.Dispose();
            }
        }

        public override void Draw(Graphics g)
        {
            _chartBottom = Chart.Rect.Bottom;
            HandleResizeEvent();
            base.Draw(g);
            if (IsRedrawRequired(g))
                base.Draw(g);

        }

        public override void HandleResizeEvent()
        {
            ScaleAxisLabels();
        }

        protected virtual bool IsRedrawRequired(Graphics g)
        {
            // Have to call HandleResizeEvent twice, since the X-scale may not be up
            // to date before calling Draw.  If nothing changes, this will be a no-op
            HandleResizeEvent();
            return (Chart.Rect.Bottom != _chartBottom);
        }

        private float _chartBottom;
        protected AxisLabelScaler _axisLabelScaler;

        protected string[] OriginalXAxisLabels
        {
            get { return _axisLabelScaler.OriginalTextLabels ?? XAxis.Scale.TextLabels; }
            set { _axisLabelScaler.OriginalTextLabels = value; }
        }

        protected void ScaleAxisLabels()
        {
            _axisLabelScaler.FirstDataIndex = FirstDataIndex;
            _axisLabelScaler.ScaleAxisLabels();
        }

        protected bool IsRepeatRemovalAllowed
        {
            get { return _axisLabelScaler.IsRepeatRemovalAllowed; }
            set { _axisLabelScaler.IsRepeatRemovalAllowed = value; }
        }

        protected void RemoveInvalidPointValues()
        {
            if (!YAxis.Scale.IsLog)
            {
                return;
            }

            foreach (var curve in CurveList)
            {
                if (curve.IsY2Axis)
                {
                    continue;
                }

                curve.Points = NonPositiveToMissing(curve.Points);
            }
        }

        /// <summary>
        /// Replace with PointPairBase.Missing all points which do not have a positive y coordinate
        /// and cannot be correctly displayed on a log scale.
        /// </summary>
        protected static IPointList NonPositiveToMissing(IPointList pointList)
        {
            int nPts = pointList.Count;
            if (!Enumerable.Range(0, nPts).Any(i => pointList[i].Y <= 0))
            {
                return pointList;
            }

            var newPoints = new List<PointPair>(pointList.Count);
            for (int i = 0; i < nPts; i++)
            {
                var pt = pointList[i];
                if (pt.Y <= 0)
                {
                    newPoints.Add(new PointPair(pt.X, PointPairBase.Missing));
                }
                else
                {
                    newPoints.Add(pt);
                }
            }

            return new PointPairList(newPoints);
        }

        protected void DrawSelectionBox(int xValue, double yMax, double yMin)
        {
            if (YAxis.Scale.IsLog)
            {
                // If it's a log scale, the bottom should be controlled by the lowest value displayed
                var minPositiveYValue = GetMinPositiveYValue();
                if (!minPositiveYValue.HasValue)
                {
                    return;
                }

                // The Y-axis is usually zoomed to Math.Pow(10.0, Math.Floor(Math.Log10(minPositiveYValue.Value)))
                // Make the selection box extend 3 orders of magnitude below the lowest value so that it goes all the way
                // even if the user zooms out a bit.
                yMin = minPositiveYValue.Value / 1000;
            }
            GraphObjList.Add(new BoxObj(xValue + .5, yMax, 0.99,
                yMin - yMax, Color.Black, Color.Empty)
            {
                IsClippedToChartRect = true,
            });
        }

        protected double? GetMinPositiveYValue()
        {
            double? minValue = null;
            foreach (var curve in CurveList)
            {
                if (curve.IsY2Axis)
                {
                    continue;
                }

                for (int i = 0; i < curve.NPts; i++)
                {
                    var pt = curve.Points[i];
                    if (pt.Y > 0)
                    {
                        if (!minValue.HasValue || minValue.Value > pt.Y)
                        {
                            minValue = pt.Y;
                        }
                    }
                }
            }

            return minValue;
        }

        #region Test Support Methods
        public string[] GetOriginalXAxisLabels()
        {
            return _axisLabelScaler.OriginalTextLabels ?? XAxis.Scale.TextLabels;
        }

        #endregion

    }
}