"""Fix pjsip transports verify column

Revision ID: 5950038a6ead
Revises: d39508cb8d8
Create Date: 2014-09-15 06:21:35.047314

"""

# revision identifiers, used by Alembic.
revision = '5950038a6ead'
down_revision = 'd39508cb8d8'

from alembic import op
import sqlalchemy as sa
from sqlalchemy.dialects.postgresql import ENUM

YESNO_NAME = 'yesno_values'
YESNO_VALUES = ['yes', 'no']

def upgrade():
    yesno_values = ENUM(*YESNO_VALUES, name=YESNO_NAME, create_type=False)

    if op.get_context().bind.dialect.name != 'mssql':
        op.alter_column('ps_transports', 'verifiy_server', type_=yesno_values,
                        new_column_name='verify_server')
    else:
        op.alter_column('ps_transports', 'verifiy_server', existing_type=yesno_values, type_=sa.String(3),
                        new_column_name='verify_server')
        yesno_values = ENUM(*YESNO_VALUES, name=YESNO_NAME, create_type=True)
        op.alter_column('ps_transports', 'verify_server', existing_type=sa.String(3), type_=yesno_values)


def downgrade():
    yesno_values = ENUM(*YESNO_VALUES, name=YESNO_NAME, create_type=False)
    if op.get_context().bind.dialect.name != 'mssql':
        op.alter_column('ps_transports', 'verify_server', type_=yesno_values,
                        new_column_name='verifiy_server')
    else:
        op.alter_column('ps_transports', 'verify_server', existing_type=yesno_values, type_=sa.String(3),
                        new_column_name='verifiy_server')
        yesno_values = ENUM(*YESNO_VALUES, name=YESNO_NAME, create_type=True)
        op.alter_column('ps_transports', 'verifiy_server', existing_type=sa.String(3), type_=yesno_values)
