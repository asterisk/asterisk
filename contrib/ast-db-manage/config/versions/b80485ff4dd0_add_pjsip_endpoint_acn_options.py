"""Add pjsip endpoint ACN options

Revision ID: b80485ff4dd0
Revises: fbb7766f17bc
Create Date: 2020-07-06 08:29:53.974820

"""

# revision identifiers, used by Alembic.
revision = 'b80485ff4dd0'
down_revision = '79290b511e4b'

from alembic import op
import sqlalchemy as sa

max_value_length = 128

def upgrade():
    op.add_column('ps_endpoints', sa.Column('incoming_offer_codec_prefs', sa.String(max_value_length)))
    op.add_column('ps_endpoints', sa.Column('outgoing_offer_codec_prefs', sa.String(max_value_length)))
    op.add_column('ps_endpoints', sa.Column('incoming_answer_codec_prefs', sa.String(max_value_length)))
    op.add_column('ps_endpoints', sa.Column('outgoing_answer_codec_prefs', sa.String(max_value_length)))


def downgrade():
    op.drop_column('ps_endpoints', 'incoming_offer_codecs_prefs')
    op.drop_column('ps_endpoints', 'outgoing_offer_codecs_prefs')
    op.drop_column('ps_endpoints', 'incoming_answer_codecs_prefs')
    op.drop_column('ps_endpoints', 'outgoing_answer_codecs_prefs')
