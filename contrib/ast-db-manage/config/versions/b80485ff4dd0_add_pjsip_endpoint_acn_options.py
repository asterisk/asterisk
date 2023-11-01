"""Add pjsip endpoint ACN options

Revision ID: b80485ff4dd0
Revises: 79290b511e4b
Create Date: 2020-07-06 08:29:53.974820

"""

# revision identifiers, used by Alembic.
revision = 'b80485ff4dd0'
down_revision = '79290b511e4b'

from alembic import op
import sqlalchemy as sa

max_value_length = 128

def upgrade():
    op.add_column('ps_endpoints', sa.Column('codec_prefs_incoming_offer', sa.String(max_value_length)))
    op.add_column('ps_endpoints', sa.Column('codec_prefs_outgoing_offer', sa.String(max_value_length)))
    op.add_column('ps_endpoints', sa.Column('codec_prefs_incoming_answer', sa.String(max_value_length)))
    op.add_column('ps_endpoints', sa.Column('codec_prefs_outgoing_answer', sa.String(max_value_length)))


def downgrade():
    op.drop_column('ps_endpoints', 'codec_prefs_incoming_offer')
    op.drop_column('ps_endpoints', 'codec_prefs_outgoing_offer')
    op.drop_column('ps_endpoints', 'codec_prefs_incoming_answer')
    op.drop_column('ps_endpoints', 'codec_prefs_outgoing_answer')
