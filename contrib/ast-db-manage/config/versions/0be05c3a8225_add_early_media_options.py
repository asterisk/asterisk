"""Add early media options

Revision ID: 0be05c3a8225
Revises: d3e4284f8707
Create Date: 2018-06-18 17:26:16.737692

"""

# revision identifiers, used by Alembic.
revision = '0be05c3a8225'
down_revision = 'd3e4284f8707'

from alembic import op
import sqlalchemy as sa
from sqlalchemy.dialects.postgresql import ENUM

YESNO_NAME = 'yesno_values'
YESNO_VALUES = ['yes', 'no']

def upgrade():
    yesno_values = ENUM(*YESNO_VALUES, name=YESNO_NAME, create_type=False)

    op.add_column('ps_systems', sa.Column('follow_early_media_fork', yesno_values))
    op.add_column('ps_systems', sa.Column('accept_multiple_sdp_answers', yesno_values))
    op.add_column('ps_endpoints', sa.Column('follow_early_media_fork', yesno_values))
    op.add_column('ps_endpoints', sa.Column('accept_multiple_sdp_answers', yesno_values))

def downgrade():
    if op.get_context().bind.dialect.name == 'mssql':
        op.drop_constraint('ck_ps_systems_follow_early_media_fork_yesno_values','ps_systems')
        op.drop_constraint('ck_ps_systems_accept_multiple_sdp_answers_yesno_values','ps_systems')
        op.drop_constraint('ck_ps_endpoints_follow_early_media_fork_yesno_values','ps_endpoints')
        op.drop_constraint('ck_ps_endpoints_accept_multiple_sdp_answers_yesno_values','ps_endpoints')
    op.drop_column('ps_systems', 'follow_early_media_fork')
    op.drop_column('ps_systems', 'accept_multiple_sdp_answers')
    op.drop_column('ps_endpoints', 'follow_early_media_fork')
    op.drop_column('ps_endpoints', 'accept_multiple_sdp_answers')
