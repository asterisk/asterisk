"""Fix tos and cos field types

Revision ID: 4c573e7135bd
Revises: 28887f25a46f
Create Date: 2014-03-05 12:16:56.618630

"""

# revision identifiers, used by Alembic.
revision = '4c573e7135bd'
down_revision = '28887f25a46f'

from alembic import op
import sqlalchemy as sa
from sqlalchemy.dialects.postgresql import ENUM

YESNO_NAME = 'yesno_values'
YESNO_VALUES = ['yes', 'no']

def upgrade():
    op.alter_column('ps_endpoints', 'tos_audio', type_=sa.String(10))
    op.alter_column('ps_endpoints', 'tos_video', type_=sa.String(10))
    op.drop_column('ps_endpoints', 'cos_audio')
    op.drop_column('ps_endpoints', 'cos_video')
    op.add_column('ps_endpoints', sa.Column('cos_audio', sa.Integer))
    op.add_column('ps_endpoints', sa.Column('cos_video', sa.Integer))

    op.alter_column('ps_transports', 'tos', type_=sa.String(10))

    # Can't cast YENO_VALUES to Integers, so dropping and adding is required
    op.drop_column('ps_transports', 'cos', schema=None, mssql_drop_check=True)
    op.add_column('ps_transports', sa.Column('cos', sa.Integer))

def downgrade():

    yesno_values = ENUM(*YESNO_VALUES, name=YESNO_NAME, create_type=False)

    # Can't cast string to YESNO_VALUES, so dropping and adding is required
    op.drop_column('ps_endpoints', 'tos_audio')
    op.drop_column('ps_endpoints', 'tos_video')
    op.add_column('ps_endpoints', sa.Column('tos_audio', yesno_values))
    op.add_column('ps_endpoints', sa.Column('tos_video', yesno_values))
    op.drop_column('ps_endpoints', 'cos_audio')
    op.drop_column('ps_endpoints', 'cos_video')
    op.add_column('ps_endpoints', sa.Column('cos_audio', yesno_values))
    op.add_column('ps_endpoints', sa.Column('cos_video', yesno_values))

    if op.get_context().bind.dialect.name == 'mssql':
        op.drop_constraint('ck_ps_transports_tos_yesno_values', 'ps_transports')
    op.drop_column('ps_transports', 'tos')
    op.add_column('ps_transports', sa.Column('tos', yesno_values))
    # Can't cast integers to YESNO_VALUES, so dropping and adding is required
    op.drop_column('ps_transports', 'cos')
    op.add_column('ps_transports', sa.Column('cos', yesno_values))
