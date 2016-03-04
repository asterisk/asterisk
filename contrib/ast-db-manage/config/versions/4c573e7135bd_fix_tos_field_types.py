"""Fix tos and cos field types

Revision ID: 4c573e7135bd
Revises: 28887f25a46f
Create Date: 2014-03-05 12:16:56.618630

"""

# revision identifiers, used by Alembic.
revision = '4c573e7135bd'
down_revision = '28887f25a46f'

from alembic import op
from alembic import context
import sqlalchemy as sa
from sqlalchemy.dialects.postgresql import ENUM

YESNO_NAME = 'yesno_values'
YESNO_VALUES = ['yes', 'no']

def upgrade():
    with op.batch_alter_table('ps_endpoints') as batch_op:
        batch_op.alter_column('tos_audio',
                    type_=sa.String(10))
        batch_op.alter_column('tos_video',
                    type_=sa.String(10))
        batch_op.drop_column('cos_audio')
        batch_op.drop_column('cos_video')
        batch_op.add_column(sa.Column('cos_audio', sa.Integer))
        batch_op.add_column(sa.Column('cos_video', sa.Integer))

    with op.batch_alter_table('ps_transports') as batch_op:
        batch_op.alter_column('tos',
                    type_=sa.String(10))

    # Can't cast YENO_VALUES to Integers, so dropping and adding is required
        batch_op.drop_column('cos')

        batch_op.add_column(sa.Column('cos', sa.Integer))

def downgrade():

    yesno_values = ENUM(*YESNO_VALUES, name=YESNO_NAME, create_type=False)

    # Can't cast string to YESNO_VALUES, so dropping and adding is required
    with op.batch_alter_table('ps_endpoints') as batch_op:
        batch_op.drop_column('tos_audio')
        batch_op.drop_column('tos_video')
        batch_op.add_column(sa.Column('tos_audio', yesno_values))
        batch_op.add_column(sa.Column('tos_video', yesno_values))
        batch_op.drop_column('cos_audio')
        batch_op.drop_column('cos_video')
        batch_op.add_column(sa.Column('cos_audio', yesno_values))
        batch_op.add_column(sa.Column('cos_video', yesno_values))

    with op.batch_alter_table('ps_transports') as batch_op:
        batch_op.drop_column('tos')
        batch_op.add_column(sa.Column('tos', yesno_values))
    # Can't cast integers to YESNO_VALUES, so dropping and adding is required
        batch_op.drop_column('cos')
        batch_op.add_column(sa.Column('cos', yesno_values))
