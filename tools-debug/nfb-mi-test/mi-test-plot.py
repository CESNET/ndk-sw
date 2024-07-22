#!/usr/bin/env python

import plotly.express as px
import plotly.graph_objects as go

import pandas as pd

names = ['length', 'src_offset', 'dst_offset', 'throughput']

#data = pd.read_csv('mi-test.csv', names=names)
#fig = px.line_3d(data, x='length', y='src_offset', z='throughput', color='dst_offset', title='MI Bandwidth')
#fig.update_scenes(camera_projection_type='orthographic')

fig = go.Figure()
files = {
    "MI write": "mi-test-wr.csv",
    "MI read": "mi-test-rd.csv",
    "MI write+read": "mi-test.csv",
}
for name, csv in files.items():
    fig.add_traces(
        px.line(pd.read_csv(csv, names=names), x="length", y="throughput")
        .update_traces(line_color=None, showlegend=True, name=name)
        .data
    )

fig.update_layout(
        title={
            'text': 'MI performance test',
            'xanchor': 'center',
            'x': 0.5,
        }, xaxis_title="MI transaction length [B]", yaxis_title="Throughput [MBps]")

fig.write_image("mi-test.png", width=1024, height=768)
#fig.show()
