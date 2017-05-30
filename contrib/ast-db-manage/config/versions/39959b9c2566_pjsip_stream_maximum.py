"""pjsip_stream_maximum

Revision ID: 39959b9c2566
Revises: d7983954dd96
Create Date: 2017-06-15 13:18:12.372333

"""

# revision identifiers, used by Alembic.
revision = '39959b9c2566'
down_revision = 'd7983954dd96'

from alembic import op
import sqlalchemy as sa


def upgrade():
    op.add_column('ps_endpoints', sa.Column('max_audio_streams', sa.Integer))
    op.add_column('ps_endpoints', sa.Column('max_video_streams', sa.Integer))


def downgrade():
    op.drop_column('ps_endpoints', 'max_audio_streams')
    op.drop_column('ps_endpoints', 'max_video_streams')
