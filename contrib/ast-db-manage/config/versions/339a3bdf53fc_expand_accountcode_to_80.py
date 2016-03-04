"""expand accountcode to 80

Revision ID: 339a3bdf53fc
Revises: 28ce1e718f05
Create Date: 2015-11-05 10:10:27.465794

"""

# revision identifiers, used by Alembic.
revision = '339a3bdf53fc'
down_revision = '28ce1e718f05'

from alembic import op
import sqlalchemy as sa


def upgrade():
    with op.batch_alter_table('ps_endpoints') as batch_op:
        batch_op.alter_column('accountcode', type_=sa.String(80))
    with op.batch_alter_table('sippeers') as batch_op:
        batch_op.alter_column('accountcode', type_=sa.String(80))
    with op.batch_alter_table('iaxfriends') as batch_op:
        batch_op.alter_column('accountcode', type_=sa.String(80))
    pass


def downgrade():
    with op.batch_alter_table('ps_endpoints') as batch_op:
        batch_op.alter_column('accountcode', type_=sa.String(20))
    with op.batch_alter_table('sippeers') as batch_op:
        batch_op.alter_column('accountcode', type_=sa.String(40))
    with op.batch_alter_table('iaxfriends') as batch_op:
        batch_op.alter_column('accountcode', type_=sa.String(20))
    pass
